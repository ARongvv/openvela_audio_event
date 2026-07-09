#!/usr/bin/env python3
"""Train a larger audio event model targeting ~100 KB TFLM arena.

Compared to train_audio_event_model.py (max ~28 KB arena on small), this
script offers wider and deeper DS-CNN configurations suitable for MCUs with
more SRAM (e.g. ESP32-S3 with 512 KB).

Architecture
------------

  Input: 49×40×C  (C=1 mel-only, C=3 with --delta-features)
    → Reshape to (49, 40, C)
    → Stem: Conv2D(5×5, stride=2) → BN → ReLU   (→ 25×20×F0)
    → DS1:  DWConv(3×3) → BN → ReLU → PWConv(1×1) → BN → ReLU → Dropout?
    → DS2:  DWConv(3×3) → BN → ReLU → PWConv(1×1) → BN → ReLU → Dropout?
    → DS3:  … (optional, controlled by config tuple length)
    → GlobalAveragePooling2D
    → Dense(num_classes, softmax)

Model sizes (config tuple = stem channels + DS-block output channels):

  ============ ================= ======= ======== ========
  model_size   channels          params  tflite   arena
  ============ ================= ======= ======== ========
  medium        [32, 48, 64]      ~8K    ~30 KB   ~75 KB
  large         [48, 64, 96]     ~15K    ~55 KB  ~110 KB
  xlarge        [64, 96, 128]    ~30K   ~100 KB  ~150 KB
  medium3       [32, 48, 64, 64] ~12K    ~40 KB   ~75 KB
  large3        [48, 64, 96, 96] ~24K    ~80 KB  ~110 KB
  ============ ================= ======= ======== ========

The script expects a directory layout like::

  datasets_segments/
    knock/*.wav
    cough/*.wav
    background/*.wav
    train/  (symlinks, optional — auto-detected)
    val/
    test/

Silence is generated automatically.
"""

from __future__ import annotations

import argparse
import csv
import functools
import json
import math
import os
import random
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

# Disable oneDNN custom ops on GPU — they cause broadcast-shape errors
# in tf.signal.stft on TF 2.21+ with CUDA compute capability 12.0a.
os.environ.setdefault("TF_ENABLE_ONEDNN_OPTS", "0")

import numpy as np
import tensorflow as tf

try:
    import scipy.signal
except ImportError as exc:  # pragma: no cover - handled at runtime
    raise SystemExit("scipy is required. Install it with: pip install scipy") from exc

try:
    from sklearn.metrics import classification_report
    from sklearn.metrics import confusion_matrix
except ImportError as exc:  # pragma: no cover - handled at runtime
    raise SystemExit(
        "scikit-learn is required. Install it with: pip install scikit-learn"
    ) from exc


# ---------------------------------------------------------------------------
# Constants (shared with TFLM AudioPreprocessor contract)
# ---------------------------------------------------------------------------
SAMPLE_RATE = 16000
CLIP_DURATION_MS = 1000
WINDOW_SIZE_MS = 30
WINDOW_STRIDE_MS = 20
FEATURE_BIN_COUNT = 40
FEATURE_FRAME_COUNT = 49
FEATURE_CHANNELS_DEFAULT = 1  # static Mel only; --delta-features sets this to 3
FEATURE_SHAPE = (FEATURE_FRAME_COUNT, FEATURE_BIN_COUNT, FEATURE_CHANNELS_DEFAULT)
FLAT_FEATURE_SIZE = FEATURE_FRAME_COUNT * FEATURE_BIN_COUNT * FEATURE_CHANNELS_DEFAULT
FFT_LENGTH = 512
QUANT_INPUT_MIN = 0.0
QUANT_INPUT_MAX = 26.0
SILENCE_TOKEN = "__generated_silence__"

# Model size presets: each entry is (stem_filters, ds1_filters, ds2_filters, …).
# The tuple length determines the number of DS-Conv blocks (len - 1).
MODEL_CONFIGS: dict[str, tuple[int, ...]] = {
    # 2-block (original sizes kept for reference)
    "micro":     (4, 8, 8),
    "tiny":      (8, 12, 16),
    "small":     (12, 16, 24),
    # 2-block (wider)
    "medium":    (32, 48, 64),
    "large":     (48, 64, 96),
    "xlarge":    (64, 96, 128),
    # 3-block (deeper)
    "medium3":   (32, 48, 64, 64),
    "large3":    (48, 64, 96, 96),
    "xlarge3":   (64, 96, 128, 128),
}


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class Sample:
    path: str
    label: int
    class_name: str


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Train a larger audio event detection model (target ~100 KB arena).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--data-dir", required=True,
                        help="Dataset root directory.")
    parser.add_argument(
        "--target-classes", required=True,
        help="Comma-separated event classes, for example: knock,cough.",
    )
    parser.add_argument(
        "--background-class", default="background",
        help="Directory name used for non-event/background audio.",
    )
    parser.add_argument(
        "--allow-missing-background", action="store_true",
        help="Allow training without a background directory. Not recommended.",
    )
    parser.add_argument(
        "--output-dir", default="output/large_model",
        help="Directory where training artifacts are written.",
    )
    parser.add_argument(
        "--model-size", default="large",
        choices=list(MODEL_CONFIGS),
        help="Model capacity preset. 'large' targets ~110 KB arena.",
    )
    parser.add_argument("--epochs", type=int, default=80,
                        help="Maximum training epochs (early-stopping applies).")
    parser.add_argument("--batch-size", type=int, default=128)
    parser.add_argument("--learning-rate", type=float, default=1e-3,
                        help="Initial learning rate for Adam.")
    parser.add_argument(
        "--lr-schedule", default="cosine",
        choices=("none", "cosine"),
        help="Learning rate decay schedule. 'cosine' decays to 0 over --epochs.",
    )
    parser.add_argument("--val-split", type=float, default=0.1)
    parser.add_argument("--test-split", type=float, default=0.1)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument(
        "--quantize", choices=("none", "dynamic", "int8"), default="int8",
        help="TFLite quantization mode.",
    )
    parser.add_argument(
        "--representative-samples-per-class", type=int, default=50,
        help="Samples per class used for full-int8 calibration.",
    )
    parser.add_argument(
        "--silence-ratio", type=float, default=0.25,
        help="Generated silence samples per real sample in each split.",
    )
    parser.add_argument(
        "--time-shift-ms", type=float, default=100.0,
        help="Maximum random time shift for training augmentation.",
    )
    parser.add_argument("--gain-min", type=float, default=0.7)
    parser.add_argument("--gain-max", type=float, default=1.3)
    parser.add_argument(
        "--noise-mix-prob", type=float, default=0.5,
        help="Probability of mixing a background sample into training audio.",
    )
    parser.add_argument("--noise-snr-min-db", type=float, default=5.0)
    parser.add_argument("--noise-snr-max-db", type=float, default=25.0)
    parser.add_argument(
        "--gaussian-noise-prob", type=float, default=0.2,
        help="Probability of adding synthetic low-level noise.",
    )
    parser.add_argument(
        "--spec-augment", action="store_true",
        help="Apply simple frequency/time masking during training.",
    )
    parser.add_argument(
        "--pcen", action="store_true",
        help="Use PCEN (Per-Channel Energy Normalization) instead of log-mel.",
    )
    parser.add_argument(
        "--pcen-alpha", type=float, default=0.5,
        help="PCEN AGC strength. Larger = more background suppression.",
    )
    parser.add_argument(
        "--pcen-delta", type=float, default=2.0,
        help="PCEN bias. Larger = preserves fainter sounds.",
    )
    parser.add_argument(
        "--pcen-root", type=float, default=0.25,
        help="PCEN root compression. Smaller = stronger compression.",
    )
    parser.add_argument(
        "--focal-loss", action="store_true",
        help="Use Focal Loss instead of standard cross-entropy.",
    )
    parser.add_argument(
        "--focal-loss-gamma", type=float, default=2.0,
        help="Gamma parameter for Focal Loss.",
    )
    parser.add_argument(
        "--delta-features", action="store_true",
        help="Include delta and delta-delta features (Mel + Δ + Δ², 3-channel input).",
    )
    parser.add_argument(
        "--se-attention", action="store_true",
        help="Add Squeeze-Excitation blocks after DS-Conv layers.",
    )
    parser.add_argument(
        "--dropout", type=float, default=0.3,
        help="Dropout rate after each DS-Conv block (0 = disabled).",
    )
    parser.add_argument(
        "--l2-weight-decay", type=float, default=1e-4,
        help="L2 regularization strength on Conv2D and Dense kernels (0 = disabled).",
    )
    parser.add_argument(
        "--no-plots", action="store_true",
        help="Skip optional PNG plot generation.",
    )
    return parser.parse_args()


# ---------------------------------------------------------------------------
# Reproducibility
# ---------------------------------------------------------------------------
def configure_runtime(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    tf.random.set_seed(seed)


# ---------------------------------------------------------------------------
# Dataset helpers
# ---------------------------------------------------------------------------
def parse_target_classes(value: str) -> list[str]:
    classes = [item.strip() for item in value.split(",") if item.strip()]
    if len(classes) < 2:
        raise ValueError("At least two target event classes are required.")
    if len(set(classes)) != len(classes):
        raise ValueError(f"Duplicate classes are not allowed: {classes}")
    return classes


def split_files(
    files: list[Path],
    val_split: float,
    test_split: float,
    rng: random.Random,
) -> tuple[list[Path], list[Path], list[Path]]:
    shuffled = list(files)
    rng.shuffle(shuffled)
    n = len(shuffled)
    if n == 0:
        return [], [], []
    if n < 3:
        return shuffled, [], []

    n_test = max(1, int(round(n * test_split)))
    n_val  = max(1, int(round(n * val_split)))
    while n - n_val - n_test < 1:
        if n_test >= n_val and n_test > 1:
            n_test -= 1
        elif n_val > 1:
            n_val -= 1
        else:
            break

    test  = shuffled[:n_test]
    val   = shuffled[n_test:n_test + n_val]
    train = shuffled[n_test + n_val:]
    return train, val, test


class DatasetIndex:
    """Scans the data directory, optionally detecting pre-split train/val/test."""

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.data_dir = Path(args.data_dir)
        self.target_classes = parse_target_classes(args.target_classes)
        self.background_class = args.background_class
        self.silence_class = "silence"
        self.class_names = self.target_classes + [
            self.background_class,
            self.silence_class,
        ]
        self.class_to_index = {
            class_name: index for index, class_name in enumerate(self.class_names)
        }
        self.index_to_class = {
            index: class_name for class_name, index in self.class_to_index.items()
        }
        self.samples_by_split: dict[str, list[Sample]] = {
            "train": [], "val": [], "test": [],
        }
        self.files_by_class: dict[str, list[Path]] = {}
        self._scan()

    # -- internals ----------------------------------------------------------
    def _scan_class(self, class_name: str, required: bool) -> list[Path]:
        class_dir = self.data_dir / class_name
        if not class_dir.is_dir():
            if required:
                raise FileNotFoundError(f"Missing dataset directory: {class_dir}")
            return []
        files = sorted(class_dir.glob("*.wav"))
        if required and not files:
            raise FileNotFoundError(f"No .wav files found in: {class_dir}")
        return files

    def _scan(self) -> None:
        if self._has_presplit():
            self._scan_from_presplit()
        else:
            self._scan_with_random_split()
        self._append_generated_silence()
        for split_name in self.samples_by_split:
            random.Random(self.args.seed).shuffle(self.samples_by_split[split_name])

    def _has_presplit(self) -> bool:
        for split_name in ("train", "val", "test"):
            split_dir = self.data_dir / split_name
            if not split_dir.is_dir():
                return False
            if not any(split_dir.glob("*/*.wav")):
                return False
        return True

    def _scan_from_presplit(self) -> None:
        classes_to_scan = list(self.target_classes) + [self.background_class]
        for split_name in ("train", "val", "test"):
            for class_name in classes_to_scan:
                class_dir = self.data_dir / split_name / class_name
                if not class_dir.is_dir():
                    if class_name != self.background_class:
                        raise FileNotFoundError(f"Missing: {class_dir}")
                    continue
                files = sorted(class_dir.glob("*.wav"))
                label = self.class_to_index[class_name]
                self.samples_by_split[split_name].extend(
                    Sample(str(path), label, class_name) for path in files
                )

    def _scan_with_random_split(self) -> None:
        rng = random.Random(self.args.seed)
        classes_to_scan = list(self.target_classes)
        background_required = not self.args.allow_missing_background
        classes_to_scan.append(self.background_class)

        for class_name in classes_to_scan:
            required = class_name != self.background_class or background_required
            files = self._scan_class(class_name, required=required)
            self.files_by_class[class_name] = files
            train, val, test = split_files(
                files, self.args.val_split, self.args.test_split, rng,
            )
            for split_name, split_files_ in (
                ("train", train), ("val", val), ("test", test),
            ):
                label = self.class_to_index[class_name]
                self.samples_by_split[split_name].extend(
                    Sample(str(path), label, class_name) for path in split_files_
                )

    def _append_generated_silence(self) -> None:
        silence_label = self.class_to_index[self.silence_class]
        for split_name, samples in self.samples_by_split.items():
            real_count = len(samples)
            silence_count = max(1, int(round(real_count * self.args.silence_ratio))) if real_count else 0
            samples.extend(
                Sample(f"{SILENCE_TOKEN}:{split_name}:{i}", silence_label, "silence")
                for i in range(silence_count)
            )

    # -- public -------------------------------------------------------------
    def summary(self) -> dict:
        by_split = {}
        for split_name, samples in self.samples_by_split.items():
            counts = defaultdict(int)
            for sample in samples:
                counts[sample.class_name] += 1
            by_split[split_name] = dict(counts)
        return {
            "data_dir": str(self.data_dir),
            "class_names": self.class_names,
            "class_to_index": self.class_to_index,
            "samples_by_split": by_split,
        }

    def write_manifest(self, output_dir: Path) -> None:
        manifest = {
            "summary": self.summary(),
            "splits": {
                split_name: [
                    {"path": s.path, "label": s.label, "class_name": s.class_name}
                    for s in samples
                ]
                for split_name, samples in self.samples_by_split.items()
            },
        }
        write_json(output_dir / "dataset_manifest.json", manifest)


# ---------------------------------------------------------------------------
# Feature extraction
# ---------------------------------------------------------------------------
class FeatureExtractor:
    def __init__(self, args: argparse.Namespace, dataset: DatasetIndex):
        self.args = args
        self.dataset = dataset
        self.background_paths = [
            s.path for s in dataset.samples_by_split["train"]
            if s.class_name == dataset.background_class
            and not s.path.startswith(SILENCE_TOKEN)
        ]
        self._mel_weight_matrix = tf.signal.linear_to_mel_weight_matrix(
            num_mel_bins=FEATURE_BIN_COUNT,
            num_spectrogram_bins=FFT_LENGTH // 2 + 1,
            sample_rate=SAMPLE_RATE,
            lower_edge_hertz=125.0,
            upper_edge_hertz=7500.0,
        )

    def load_audio(self, path: str, training: bool) -> np.ndarray:
        if path.startswith(SILENCE_TOKEN):
            return self._generated_silence(training)

        audio_binary = tf.io.read_file(path)
        audio, sample_rate = tf.audio.decode_wav(audio_binary, desired_channels=1)
        audio_np = np.squeeze(audio.numpy(), axis=-1).astype(np.float32)
        sample_rate_value = int(sample_rate.numpy())
        if sample_rate_value != SAMPLE_RATE:
            audio_np = self._resample(audio_np, sample_rate_value, SAMPLE_RATE)
        audio_np = self._crop_or_pad(audio_np, training)
        return np.clip(audio_np, -1.0, 1.0).astype(np.float32)

    def _generated_silence(self, training: bool) -> np.ndarray:
        audio = np.zeros(SAMPLE_RATE, dtype=np.float32)
        if training and random.random() < 0.5:
            audio += np.random.normal(0.0, 0.001, size=SAMPLE_RATE).astype(np.float32)
        return audio

    @staticmethod
    def _resample(audio: np.ndarray, source_rate: int, target_rate: int) -> np.ndarray:
        divisor = math.gcd(source_rate, target_rate)
        up = target_rate // divisor
        down = source_rate // divisor
        return scipy.signal.resample_poly(audio, up, down).astype(np.float32)

    @staticmethod
    def _crop_or_pad(audio: np.ndarray, training: bool) -> np.ndarray:
        target_len = SAMPLE_RATE * CLIP_DURATION_MS // 1000
        if len(audio) > target_len:
            if training:
                start = random.randint(0, len(audio) - target_len)
            else:
                start = (len(audio) - target_len) // 2
            return audio[start:start + target_len]
        if len(audio) < target_len:
            output = np.zeros(target_len, dtype=np.float32)
            start = random.randint(0, target_len - len(audio)) if training else 0
            output[start:start + len(audio)] = audio
            return output
        return audio

    def augment_audio(self, audio: np.ndarray, class_name: str) -> np.ndarray:
        if self.args.time_shift_ms > 0:
            max_shift = int(SAMPLE_RATE * self.args.time_shift_ms / 1000)
            shift = random.randint(-max_shift, max_shift)
            audio = shift_with_zeros(audio, shift)

        gain = random.uniform(self.args.gain_min, self.args.gain_max)
        audio = audio * gain

        if (
            self.background_paths
            and class_name != self.dataset.background_class
            and random.random() < self.args.noise_mix_prob
        ):
            bg_path = random.choice(self.background_paths)
            background = self.load_audio(bg_path, training=True)
            snr_db = random.uniform(
                self.args.noise_snr_min_db, self.args.noise_snr_max_db,
            )
            audio = mix_with_snr(audio, background, snr_db)

        if random.random() < self.args.gaussian_noise_prob:
            noise_std = random.uniform(0.0005, 0.005)
            audio = audio + np.random.normal(0.0, noise_std, size=audio.shape)

        return np.clip(audio, -1.0, 1.0).astype(np.float32)

    @staticmethod
    def _apply_pcen(mel_energy: np.ndarray, alpha: float = 0.5,
                    delta: float = 2.0, root: float = 0.25,
                    smooth_coeff: float = 0.05, eps: float = 1e-6) -> np.ndarray:
        """Per-Channel Energy Normalization (Wang et al., 2017).

        PCEN(mel) = (mel / (eps + smooth)^alpha + delta)^root - delta^root
        smooth[t] = (1 - s) * smooth[t-1] + s * mel[t]    (EMA)
        """
        smooth = np.zeros_like(mel_energy, dtype=np.float32)
        smooth[0] = mel_energy[0]
        inv_coeff = 1.0 - smooth_coeff
        for t in range(1, mel_energy.shape[0]):
            smooth[t] = inv_coeff * smooth[t - 1] + smooth_coeff * mel_energy[t]
        gain = np.power(eps + smooth, alpha)
        return np.power(mel_energy / gain + delta, root) - np.power(delta, root)

    def make_feature(self, audio: np.ndarray, training: bool) -> np.ndarray:
        if len(audio) < SAMPLE_RATE * WINDOW_SIZE_MS // 1000:
            raise ValueError(
                f"Audio too short for STFT: {len(audio)} samples, "
                f"need >= {SAMPLE_RATE * WINDOW_SIZE_MS // 1000}"
            )
        audio_tensor = tf.convert_to_tensor(audio, dtype=tf.float32)
        frame_length = WINDOW_SIZE_MS * SAMPLE_RATE // 1000
        frame_step   = WINDOW_STRIDE_MS * SAMPLE_RATE // 1000
        # Run STFT on CPU — GPU oneDNN kernels cause broadcast-shape errors
        # in TF 2.21+ with certain CUDA archs.
        with tf.device("/CPU:0"):
            stft = tf.signal.stft(
                audio_tensor,
                frame_length=frame_length,
                frame_step=frame_step,
                fft_length=FFT_LENGTH,
                window_fn=functools.partial(tf.signal.hann_window, periodic=True),
                pad_end=False,
            )
        magnitude = tf.abs(stft)
        mel = tf.matmul(magnitude, self._mel_weight_matrix)

        if self.args.pcen:
            feature = self._apply_pcen(
                mel.numpy().astype(np.float32),
                alpha=self.args.pcen_alpha,
                delta=self.args.pcen_delta,
                root=self.args.pcen_root,
            )
            feature = (feature + 1.2) * 8.0
            feature = tf.clip_by_value(
                tf.convert_to_tensor(feature), QUANT_INPUT_MIN, QUANT_INPUT_MAX,
            )
        else:
            log_mel = tf.math.log(mel + 1e-6)
            feature = (log_mel + 12.0) * 1.625
            feature = tf.clip_by_value(feature, QUANT_INPUT_MIN, QUANT_INPUT_MAX)

        feature_np = feature.numpy().astype(np.float32)
        if training and self.args.spec_augment:
            feature_np = apply_spec_augment(feature_np)

        if self.args.delta_features:
            delta = np.zeros_like(feature_np)
            delta[1:] = feature_np[1:] - feature_np[:-1]
            delta2 = np.zeros_like(delta)
            delta2[1:] = delta[1:] - delta[:-1]
            feature_np = np.stack([feature_np, delta, delta2], axis=-1)  # (49,40,3)
        else:
            feature_np = np.expand_dims(feature_np, axis=-1)  # (49,40,1)

        flat = feature_np.reshape(-1).astype(np.float32)
        channels = 3 if self.args.delta_features else 1
        expected = FEATURE_FRAME_COUNT * FEATURE_BIN_COUNT * channels
        if len(flat) < expected:
            padded = np.zeros(expected, dtype=np.float32)
            padded[:len(flat)] = flat
            return padded
        return flat[:expected]


# ---------------------------------------------------------------------------
# Audio augmentations
# ---------------------------------------------------------------------------
def shift_with_zeros(audio: np.ndarray, shift: int) -> np.ndarray:
    if shift == 0:
        return audio
    output = np.zeros_like(audio)
    if shift > 0:
        output[shift:] = audio[:-shift]
    else:
        output[:shift] = audio[-shift:]
    return output


def mix_with_snr(audio: np.ndarray, noise: np.ndarray, snr_db: float) -> np.ndarray:
    audio_power = float(np.mean(np.square(audio))) + 1e-9
    noise_power = float(np.mean(np.square(noise))) + 1e-9
    desired_noise_power = audio_power / (10.0 ** (snr_db / 10.0))
    noise_scale = math.sqrt(desired_noise_power / noise_power)
    return audio + noise * noise_scale


# ---------------------------------------------------------------------------
# Loss
# ---------------------------------------------------------------------------
def _focal_loss(gamma: float = 2.0):
    import tensorflow.keras.backend as K

    def loss(y_true: tf.Tensor, y_pred: tf.Tensor) -> tf.Tensor:
        y_pred = tf.clip_by_value(y_pred, K.epsilon(), 1.0 - K.epsilon())
        y_true = tf.cast(tf.reshape(y_true, [-1]), tf.int32)
        y_true_oh = tf.one_hot(y_true, depth=tf.shape(y_pred)[-1])
        p_t = tf.reduce_sum(y_true_oh * y_pred, axis=-1)
        return -tf.pow(1.0 - p_t, gamma) * tf.math.log(p_t)

    return loss


# ---------------------------------------------------------------------------
# SpecAugment
# ---------------------------------------------------------------------------
def apply_spec_augment(feature: np.ndarray) -> np.ndarray:
    augmented = feature.copy()
    if random.random() < 0.5:
        width = random.randint(2, 6)
        start = random.randint(0, max(0, FEATURE_BIN_COUNT - width))
        augmented[:, start:start + width] = 0.0
    if random.random() < 0.5:
        width = random.randint(2, 8)
        start = random.randint(0, max(0, FEATURE_FRAME_COUNT - width))
        augmented[start:start + width, :] = 0.0
    return augmented


# ---------------------------------------------------------------------------
# tf.data pipeline
# ---------------------------------------------------------------------------
def make_dataset(
    samples: list[Sample],
    extractor: FeatureExtractor,
    batch_size: int,
    training: bool,
    flat_feature_size: int | None = None,
) -> tf.data.Dataset:
    if flat_feature_size is None:
        flat_feature_size = FLAT_FEATURE_SIZE

    def generator() -> Iterable[tuple[np.ndarray, np.int32]]:
        local_samples = list(samples)
        if training:
            random.shuffle(local_samples)
        for sample in local_samples:
            audio = extractor.load_audio(sample.path, training=training)
            if training:
                audio = extractor.augment_audio(audio, sample.class_name)
            feature = extractor.make_feature(audio, training=training)
            yield feature, np.int32(sample.label)

    dataset = tf.data.Dataset.from_generator(
        generator,
        output_signature=(
            tf.TensorSpec(shape=(flat_feature_size,), dtype=tf.float32),
            tf.TensorSpec(shape=(), dtype=tf.int32),
        ),
    )
    if training:
        dataset = dataset.shuffle(
            max(32, len(samples) * 2), reshuffle_each_iteration=True,
        )
        dataset = dataset.batch(batch_size, drop_remainder=False)
    else:
        dataset = dataset.batch(batch_size, drop_remainder=False)
    return dataset.prefetch(tf.data.AUTOTUNE)


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------
def build_model(
    num_classes: int,
    model_size: str,
    num_channels: int = 1,
    use_se: bool = False,
    dropout_rate: float = 0.0,
    l2_strength: float = 0.0,
) -> tf.keras.Model:
    """Build a DS-CNN with variable depth controlled by MODEL_CONFIGS[model_size].

    The config tuple is (stem, ds1_out, ds2_out, …, dsN_out).  Number of
    DS-Conv blocks = len(tuple) - 1.
    """
    config = MODEL_CONFIGS[model_size]
    stem_filters = config[0]
    ds_filters   = config[1:]   # one entry per DS block
    num_ds_blocks = len(ds_filters)

    l2 = tf.keras.regularizers.l2(l2_strength) if l2_strength > 0 else None

    flat_size   = FEATURE_FRAME_COUNT * FEATURE_BIN_COUNT * num_channels
    input_shape = (FEATURE_FRAME_COUNT, FEATURE_BIN_COUNT, num_channels)

    inputs = tf.keras.Input(shape=(flat_size,), name="input_features")
    x = tf.keras.layers.Reshape(input_shape, name="feature_image")(inputs)

    # Stem
    x = conv_bn_relu(x, stem_filters, kernel_size=5, strides=2,
                     name="stem", l2=l2)

    # DS-Conv blocks
    for idx, filters in enumerate(ds_filters, start=1):
        # Default stride 1; could optionally add stride=2 for blocks beyond
        # a certain depth to down-sample further.
        x = ds_conv_block(x, filters, strides=1, name=f"ds{idx}", l2=l2)
        if use_se:
            x = se_block(x, name=f"se{idx}")
        if dropout_rate > 0:
            x = tf.keras.layers.Dropout(
                dropout_rate, name=f"dropout{idx}",
            )(x)

    # Head
    x = tf.keras.layers.GlobalAveragePooling2D(name="global_average_pool")(x)
    if dropout_rate > 0:
        x = tf.keras.layers.Dropout(dropout_rate, name="dropout_final")(x)
    outputs = tf.keras.layers.Dense(
        num_classes, activation="softmax", name="probabilities",
        kernel_regularizer=l2,
    )(x)

    return tf.keras.Model(
        inputs=inputs, outputs=outputs,
        name=f"audio_event_{model_size}",
    )


def conv_bn_relu(
    x: tf.Tensor,
    filters: int,
    kernel_size: int,
    strides: int,
    name: str,
    l2: tf.keras.regularizers.Regularizer | None = None,
) -> tf.Tensor:
    x = tf.keras.layers.Conv2D(
        filters, (kernel_size, kernel_size),
        strides=(strides, strides),
        padding="same", use_bias=False,
        kernel_regularizer=l2,
        name=f"{name}_conv",
    )(x)
    x = tf.keras.layers.BatchNormalization(name=f"{name}_bn")(x)
    return tf.keras.layers.ReLU(name=f"{name}_relu")(x)


def ds_conv_block(
    x: tf.Tensor,
    filters: int,
    strides: int,
    name: str,
    l2: tf.keras.regularizers.Regularizer | None = None,
) -> tf.Tensor:
    x = tf.keras.layers.DepthwiseConv2D(
        (3, 3), strides=(strides, strides),
        padding="same", use_bias=False,
        depthwise_regularizer=l2,
        name=f"{name}_dwconv",
    )(x)
    x = tf.keras.layers.BatchNormalization(name=f"{name}_dw_bn")(x)
    x = tf.keras.layers.ReLU(name=f"{name}_dw_relu")(x)
    x = tf.keras.layers.Conv2D(
        filters, (1, 1),
        padding="same", use_bias=False,
        kernel_regularizer=l2,
        name=f"{name}_pwconv",
    )(x)
    x = tf.keras.layers.BatchNormalization(name=f"{name}_pw_bn")(x)
    return tf.keras.layers.ReLU(name=f"{name}_pw_relu")(x)


def se_block(x: tf.Tensor, ratio: int = 4, name: str = "se") -> tf.Tensor:
    """Squeeze-Excitation: learn per-channel weights based on global context."""
    channels = x.shape[-1]
    s = tf.keras.layers.GlobalAveragePooling2D(name=f"{name}_gap")(x)
    s = tf.keras.layers.Dense(
        max(1, channels // ratio), activation="relu", name=f"{name}_fc1",
    )(s)
    s = tf.keras.layers.Dense(
        channels, activation="sigmoid", name=f"{name}_fc2",
    )(s)
    return tf.keras.layers.Multiply(name=f"{name}_scale")([x, s])


# ---------------------------------------------------------------------------
# Training utilities
# ---------------------------------------------------------------------------
def compute_class_weight(samples: list[Sample], num_classes: int) -> dict[int, float]:
    counts = np.zeros(num_classes, dtype=np.float32)
    for sample in samples:
        counts[sample.label] += 1
    total = float(np.sum(counts))
    weights = {}
    for index, count in enumerate(counts):
        if count > 0:
            weights[index] = total / (num_classes * float(count))
    return weights


def _build_lr_schedule(args: argparse.Namespace, steps_per_epoch: int):
    """Return a LearningRateSchedule or None (for constant LR)."""
    if args.lr_schedule == "none":
        return args.learning_rate

    if args.lr_schedule == "cosine":
        total_steps = steps_per_epoch * args.epochs
        return tf.keras.optimizers.schedules.CosineDecay(
            initial_learning_rate=args.learning_rate,
            decay_steps=total_steps,
            alpha=0.0,  # decay to 0
        )
    return args.learning_rate


# ---------------------------------------------------------------------------
# Main training routine
# ---------------------------------------------------------------------------
def train(args: argparse.Namespace) -> None:
    configure_runtime(args.seed)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    dataset_index = DatasetIndex(args)
    extractor = FeatureExtractor(args, dataset_index)
    dataset_index.write_manifest(output_dir)
    write_labels(output_dir / "labels.txt", dataset_index.class_names)
    if not args.no_plots:
        maybe_write_feature_samples(
            output_dir / "feature_samples.png", extractor, dataset_index,
        )

    print("Dataset summary:")
    print(json.dumps(dataset_index.summary(), indent=2, ensure_ascii=False))

    train_samples = dataset_index.samples_by_split["train"]
    val_samples   = dataset_index.samples_by_split["val"]
    test_samples  = dataset_index.samples_by_split["test"]
    if not train_samples:
        raise RuntimeError("Training split is empty.")

    num_channels   = 3 if args.delta_features else 1
    flat_feat_size = FEATURE_FRAME_COUNT * FEATURE_BIN_COUNT * num_channels

    train_ds = make_dataset(
        train_samples, extractor, args.batch_size, training=True,
        flat_feature_size=flat_feat_size,
    )
    val_ds = make_dataset(
        val_samples, extractor, args.batch_size, training=False,
        flat_feature_size=flat_feat_size,
    )
    test_ds = make_dataset(
        test_samples, extractor, args.batch_size, training=False,
        flat_feature_size=flat_feat_size,
    )

    model = build_model(
        len(dataset_index.class_names), args.model_size,
        num_channels=num_channels, use_se=args.se_attention,
        dropout_rate=args.dropout, l2_strength=args.l2_weight_decay,
    )
    model.summary()

    if args.focal_loss:
        print(f"  Using Focal Loss (gamma={args.focal_loss_gamma})")
        loss_fn = _focal_loss(gamma=args.focal_loss_gamma)
    else:
        loss_fn = tf.keras.losses.SparseCategoricalCrossentropy()

    steps_per_epoch = max(1, len(train_samples) // args.batch_size)
    lr = _build_lr_schedule(args, steps_per_epoch)
    if isinstance(lr, tf.keras.optimizers.schedules.LearningRateSchedule):
        print(f"  Using cosine LR schedule: {args.learning_rate} → 0 "
              f"over {args.epochs} epochs")

    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=lr),
        loss=loss_fn,
        metrics=["accuracy"],
    )

    monitor_metric = "val_accuracy" if val_samples else "accuracy"
    callbacks = [
        tf.keras.callbacks.ModelCheckpoint(
            filepath=str(output_dir / "best_weights.weights.h5"),
            monitor=monitor_metric,
            save_best_only=True,
            save_weights_only=True,
            verbose=1,
        ),
        tf.keras.callbacks.EarlyStopping(
            monitor=monitor_metric,
            patience=15,
            restore_best_weights=True,
            verbose=1,
        ),
        tf.keras.callbacks.CSVLogger(
            str(output_dir / "training_history.csv"),
        ),
    ]

    class_weight = compute_class_weight(
        train_samples, len(dataset_index.class_names),
    )
    history = model.fit(
        train_ds,
        validation_data=val_ds if val_samples else None,
        epochs=args.epochs,
        callbacks=callbacks,
        class_weight=class_weight,
        verbose=1,
    )
    write_training_summary(output_dir / "training_summary.json", history.history)
    if not args.no_plots:
        maybe_write_training_curves(
            output_dir / "training_curves.png", history.history,
        )

    best_weights_path = output_dir / "best_weights.weights.h5"
    if best_weights_path.exists():
        model.load_weights(str(best_weights_path))

    metrics = evaluate_model(
        model, test_ds, dataset_index.class_names,
        non_event_names=(dataset_index.background_class, dataset_index.silence_class),
        output_dir=output_dir, args=args,
    )
    export_models(model, extractor, dataset_index, output_dir, args)

    # Evaluate int8 TFLite accuracy
    tflite_path = output_dir / "model_int8.tflite"
    if tflite_path.exists() and test_samples:
        print("Evaluating int8 TFLite model ...")
        tflite_metrics = evaluate_tflite_int8(
            tflite_path, test_ds, dataset_index.class_names,
        )
        write_json(output_dir / "metrics_tflite_int8.json", tflite_metrics)
        print(
            f"  int8 accuracy: {tflite_metrics['test_accuracy']:.4f}  "
            f"(Keras: {metrics['test_accuracy']:.4f})"
        )

    write_metadata(model, dataset_index, metrics, output_dir, args)
    print(f"Done. Artifacts written to: {output_dir}")


# ---------------------------------------------------------------------------
# Evaluation
# ---------------------------------------------------------------------------
def evaluate_model(
    model: tf.keras.Model,
    test_ds: tf.data.Dataset,
    class_names: list[str],
    non_event_names: tuple[str, str],
    output_dir: Path,
    args: argparse.Namespace,
) -> dict:
    y_true: list[int] = []
    y_pred: list[int] = []
    y_score: list[list[float]] = []
    for features, labels in test_ds:
        probabilities = model.predict(features, verbose=0)
        y_true.extend(labels.numpy().astype(int).tolist())
        y_pred.extend(np.argmax(probabilities, axis=1).astype(int).tolist())
        y_score.extend(probabilities.astype(float).tolist())

    if not y_true:
        metrics = {"warning": "Test split is empty."}
        write_json(output_dir / "metrics.json", metrics)
        return metrics

    labels = list(range(len(class_names)))
    cm = confusion_matrix(y_true, y_pred, labels=labels)
    report = classification_report(
        y_true, y_pred, labels=labels, target_names=class_names,
        output_dict=True, zero_division=0,
    )
    write_confusion_csv(output_dir / "confusion_matrix.csv", cm, class_names)
    if not args.no_plots:
        maybe_write_confusion_plot(
            output_dir / "confusion_matrix.png", cm, class_names,
        )
        if y_score and len(class_names) > 1:
            maybe_write_roc_curves(
                output_dir / "roc_curves.png", y_true, y_score, class_names,
            )

    non_event_name_set = set(non_event_names)
    event_indices = [
        i for i, name in enumerate(class_names)
        if name not in non_event_name_set
    ]
    non_event_indices = [
        i for i, name in enumerate(class_names)
        if name in non_event_name_set
    ]
    false_alarm_count = sum(
        1 for truth, pred in zip(y_true, y_pred)
        if truth in non_event_indices and pred in event_indices
    )
    non_event_count = sum(1 for truth in y_true if truth in non_event_indices)
    miss_count = sum(
        1 for truth, pred in zip(y_true, y_pred)
        if truth in event_indices and pred not in event_indices
    )
    event_count = sum(1 for truth in y_true if truth in event_indices)

    metrics = {
        "test_accuracy": float(report["accuracy"]),
        "false_alarm_rate": safe_div(false_alarm_count, non_event_count),
        "miss_rate": safe_div(miss_count, event_count),
        "classification_report": report,
        "confusion_matrix": cm.tolist(),
        "num_test_samples": len(y_true),
        "raw_predictions": {
            "labels": y_true,
            "predictions": y_pred,
            "probabilities": y_score,
        },
    }
    write_json(output_dir / "metrics.json", metrics)
    if y_score:
        write_threshold_sweep(
            output_dir / "threshold_sweep.json",
            y_true, y_score, class_names, event_indices, non_event_indices,
        )
    print(json.dumps(
        {k: metrics[k] for k in ("test_accuracy", "false_alarm_rate", "miss_rate")},
        indent=2,
    ))
    return metrics


def write_threshold_sweep(
    path: Path,
    y_true: list[int],
    y_score: list[list[float]],
    class_names: list[str],
    event_indices: list[int],
    non_event_indices: list[int],
) -> None:
    """Sweep per-class thresholds and evaluate deployment-aligned metrics."""
    thresholds = np.arange(0.30, 1.0, 0.05).tolist()
    sweep: dict[str, list[dict]] = {}
    for cls_idx, cls_name in enumerate(class_names):
        cls_sweep = []
        for thresh in thresholds:
            pred_event = [s[cls_idx] >= thresh for s in y_score]
            tp = sum(int(t == cls_idx and p) for t, p in zip(y_true, pred_event))
            fp = sum(int(t != cls_idx and p) for t, p in zip(y_true, pred_event))
            fn = sum(int(t == cls_idx and not p) for t, p in zip(y_true, pred_event))
            support = sum(1 for t in y_true if t == cls_idx)
            precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
            recall = tp / support if support > 0 else 0.0
            cls_sweep.append({
                "threshold": round(thresh, 2),
                "tp": tp, "fp": fp, "fn": fn,
                "precision": round(precision, 4),
                "recall": round(recall, 4),
                "f1": round(
                    2 * precision * recall / (precision + recall), 4,
                ) if (precision + recall) > 0 else 0.0,
            })
        sweep[cls_name] = cls_sweep

    # Overall event-level: any event class >= threshold
    overall_sweep = []
    for thresh in thresholds:
        pred_event = [
            max(s[i] for i in event_indices) >= thresh for s in y_score
        ]
        tp = sum(
            int(t in event_indices and p)
            for t, p in zip(y_true, pred_event)
        )
        fp = sum(
            int(t in non_event_indices and p)
            for t, p in zip(y_true, pred_event)
        )
        fn = sum(
            int(t in event_indices and not p)
            for t, p in zip(y_true, pred_event)
        )
        ec = sum(1 for t in y_true if t in event_indices)
        nc = sum(1 for t in y_true if t in non_event_indices)
        overall_sweep.append({
            "threshold": round(thresh, 2),
            "false_alarm_rate": round(fp / nc, 4) if nc else 0,
            "miss_rate": round(fn / ec, 4) if ec else 0,
        })
    sweep["_overall_"] = overall_sweep

    # Consecutive N-frame trigger simulation
    consecutive_sweep = _consecutive_trigger_sweep(
        y_true, y_score, event_indices, non_event_indices, thresholds,
    )
    sweep["_consecutive_"] = consecutive_sweep

    write_json(path, sweep)


def _consecutive_trigger_sweep(
    y_true: list[int],
    y_score: list[list[float]],
    event_indices: list[int],
    non_event_indices: list[int],
    thresholds: list[float],
    n_frames: int = 2,
) -> list[dict]:
    """Simulate consecutive-frame triggering for deployment-aligned metrics."""
    results = []
    for thresh in thresholds:
        scores = np.array([max(s[i] for i in event_indices) for s in y_score])
        triggered = np.zeros(len(scores), dtype=bool)
        count = 0
        for i in range(len(scores)):
            if scores[i] >= thresh:
                count += 1
            else:
                count = 0
            if count >= n_frames and i >= n_frames - 1:
                triggered[max(0, i - n_frames + 1):i + 1] = True

        tp = sum(
            int(t in event_indices and triggered[i])
            for i, t in enumerate(y_true)
        )
        fp = sum(
            int(t in non_event_indices and triggered[i])
            for i, t in enumerate(y_true)
        )
        fn = sum(
            int(t in event_indices and not triggered[i])
            for i, t in enumerate(y_true)
        )
        ec = sum(1 for t in y_true if t in event_indices)
        nc = sum(1 for t in y_true if t in non_event_indices)
        results.append({
            "threshold": round(thresh, 2),
            "n_frames": n_frames,
            "false_alarm_rate": round(fp / nc, 4) if nc else 0,
            "miss_rate": round(fn / ec, 4) if ec else 0,
        })
    return results


def evaluate_tflite_int8(
    tflite_path: Path,
    test_ds: tf.data.Dataset,
    class_names: list[str],
) -> dict:
    """Evaluate int8 TFLite model on the test set."""
    interpreter = tf.lite.Interpreter(model_path=str(tflite_path))
    interpreter.allocate_tensors()
    input_details  = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    input_scale, input_zero_point     = input_details[0]["quantization"]
    output_scale, output_zero_point   = output_details[0]["quantization"]

    y_true, y_pred = [], []
    for features, labels in test_ds:
        for i in range(features.shape[0]):
            feat = features[i].numpy().astype(np.float32)
            feat_int8 = np.clip(
                np.round(feat / input_scale) + input_zero_point, -128, 127,
            ).astype(np.int8)
            interpreter.set_tensor(
                input_details[0]["index"], np.expand_dims(feat_int8, 0),
            )
            interpreter.invoke()
            out_int8 = interpreter.get_tensor(output_details[0]["index"])
            out_float = (out_int8.astype(np.float32) - output_zero_point) * output_scale
            y_pred.append(int(np.argmax(out_float[0])))
            y_true.append(int(labels[i].numpy()))

    cm = confusion_matrix(
        y_true, y_pred, labels=list(range(len(class_names))),
    )
    report = classification_report(
        y_true, y_pred, labels=list(range(len(class_names))),
        target_names=class_names, output_dict=True, zero_division=0,
    )
    accuracy = np.mean(np.array(y_pred) == np.array(y_true))
    return {
        "test_accuracy": round(float(accuracy), 4),
        "classification_report": report,
        "confusion_matrix": cm.tolist(),
        "num_test_samples": len(y_true),
    }


# ---------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------
def export_models(
    model: tf.keras.Model,
    extractor: FeatureExtractor,
    dataset_index: DatasetIndex,
    output_dir: Path,
    args: argparse.Namespace,
) -> None:
    saved_model_dir = output_dir / "saved_model"
    tf.saved_model.save(model, str(saved_model_dir))

    float_converter = tf.lite.TFLiteConverter.from_keras_model(model)
    float_tflite = float_converter.convert()
    (output_dir / "model_float.tflite").write_bytes(float_tflite)

    if args.quantize == "none":
        return

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    if args.quantize == "int8":
        converter.representative_dataset = lambda: representative_dataset(
            extractor, dataset_index, args.representative_samples_per_class,
        )
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type = tf.int8
        converter.inference_output_type = tf.int8
    quantized_tflite = converter.convert()
    quant_path = output_dir / (
        "model_int8.tflite" if args.quantize == "int8" else "model_dynamic.tflite"
    )
    quant_path.write_bytes(quantized_tflite)
    export_c_array(
        quantized_tflite,
        output_dir / "model.cc",
        output_dir / "model.h",
        array_name="g_audio_event_model",
    )


def representative_dataset(
    extractor: FeatureExtractor,
    dataset_index: DatasetIndex,
    samples_per_class: int,
):
    selected: list[Sample] = []
    by_class: dict[str, list[Sample]] = defaultdict(list)
    for sample in dataset_index.samples_by_split["train"]:
        by_class[sample.class_name].append(sample)
    for class_name in dataset_index.class_names:
        selected.extend(by_class[class_name][:samples_per_class])
    if not selected:
        selected = dataset_index.samples_by_split["train"][:samples_per_class]
    for sample in selected:
        audio = extractor.load_audio(sample.path, training=False)
        feature = extractor.make_feature(audio, training=False)
        yield [np.expand_dims(feature, axis=0).astype(np.float32)]


# ---------------------------------------------------------------------------
# Metadata & artifact writers
# ---------------------------------------------------------------------------
def write_metadata(
    model: tf.keras.Model,
    dataset_index: DatasetIndex,
    metrics: dict,
    output_dir: Path,
    args: argparse.Namespace,
) -> None:
    metadata = {
        "sample_rate": SAMPLE_RATE,
        "clip_duration_ms": CLIP_DURATION_MS,
        "window_size_ms": WINDOW_SIZE_MS,
        "window_stride_ms": WINDOW_STRIDE_MS,
        "feature_bins": FEATURE_BIN_COUNT,
        "feature_shape": [
            FEATURE_FRAME_COUNT, FEATURE_BIN_COUNT,
            3 if args.delta_features else 1,
        ],
        "flat_feature_size": (
            FEATURE_FRAME_COUNT * FEATURE_BIN_COUNT
            * (3 if args.delta_features else 1)
        ),
        "classes": dataset_index.class_names,
        "class_to_index": dataset_index.class_to_index,
        "model_size": args.model_size,
        "model_config": list(MODEL_CONFIGS[args.model_size]),
        "parameter_count": int(model.count_params()),
        "quantization": args.quantize,
        "dropout": args.dropout,
        "l2_weight_decay": args.l2_weight_decay,
        "feature_backend": "tensorflow_log_mel",
        "threshold_recommendation": {
            class_name: 0.75
            for class_name in dataset_index.target_classes
        },
        "trigger_recommendation": (
            "alarm after probability >= 0.75 for 2 consecutive inferences"
        ),
        "metrics": {
            key: metrics[key]
            for key in ("test_accuracy", "false_alarm_rate", "miss_rate")
            if key in metrics
        },
    }
    write_json(output_dir / "metadata.json", metadata)


def write_labels(path: Path, class_names: list[str]) -> None:
    path.write_text("\n".join(class_names) + "\n", encoding="utf-8")


def write_training_summary(path: Path, history: dict) -> None:
    serializable = {
        key: [float(item) for item in value]
        for key, value in history.items()
    }
    write_json(path, serializable)


def write_confusion_csv(
    path: Path, matrix: np.ndarray, class_names: list[str],
) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["truth/pred"] + class_names)
        for class_name, row in zip(class_names, matrix):
            writer.writerow([class_name] + [int(value) for value in row])


# ---------------------------------------------------------------------------
# Optional plots
# ---------------------------------------------------------------------------
def maybe_write_training_curves(
    path: Path, history: dict[str, list[float]],
) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is not installed; skipping training_curves.png")
        return
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4))
    epochs = range(1, len(history["loss"]) + 1)
    ax1.plot(epochs, history["loss"], label="Train Loss", linewidth=1.5)
    if "val_loss" in history:
        ax1.plot(epochs, history["val_loss"], label="Val Loss", linewidth=1.5)
    ax1.set_xlabel("Epoch")
    ax1.set_ylabel("Loss")
    ax1.set_title("Loss")
    ax1.legend()
    ax2.plot(epochs, history["accuracy"], label="Train Acc", linewidth=1.5)
    if "val_accuracy" in history:
        ax2.plot(epochs, history["val_accuracy"], label="Val Acc", linewidth=1.5)
    ax2.set_xlabel("Epoch")
    ax2.set_ylabel("Accuracy")
    ax2.set_title("Accuracy")
    ax2.legend()
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def maybe_write_roc_curves(
    path: Path,
    y_true: list[int],
    y_score: list[list[float]],
    class_names: list[str],
) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is not installed; skipping roc_curves.png")
        return
    try:
        from sklearn.preprocessing import label_binarize
        from sklearn.metrics import roc_curve, auc
    except ImportError:
        print("scikit-learn metric helpers not available; skipping roc_curves.png")
        return

    n_classes = len(class_names)
    y_true_bin = label_binarize(y_true, classes=list(range(n_classes)))

    fig, ax = plt.subplots(figsize=(6, 5))
    colors = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728"]
    for i, name in enumerate(class_names):
        if n_classes > 2:
            fpr, tpr, _ = roc_curve(y_true_bin[:, i], [s[i] for s in y_score])
        else:
            fpr, tpr, _ = roc_curve(y_true_bin, [s[1] for s in y_score])
        roc_auc = auc(fpr, tpr)
        ax.plot(
            fpr, tpr, color=colors[i % len(colors)], linewidth=1.5,
            label=f"{name} (AUC={roc_auc:.3f})",
        )
    ax.plot([0, 1], [0, 1], "k--", linewidth=0.8, alpha=0.5)
    ax.set_xlim([0.0, 1.0])
    ax.set_ylim([0.0, 1.05])
    ax.set_xlabel("False Positive Rate")
    ax.set_ylabel("True Positive Rate")
    ax.set_title("ROC Curves")
    ax.legend(loc="lower right", fontsize=8)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def maybe_write_feature_samples(
    path: Path,
    extractor: FeatureExtractor,
    dataset_index: DatasetIndex,
) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is not installed; skipping feature_samples.png")
        return

    class_names = [c for c in dataset_index.class_names if c != "silence"]
    n = len(class_names)
    fig, axes = plt.subplots(1, n, figsize=(3 * n, 3))
    if n == 1:
        axes = [axes]

    for ax, cls in zip(axes, class_names):
        samples = [
            s for s in dataset_index.samples_by_split["train"]
            if s.class_name == cls
        ]
        if not samples:
            ax.set_title(f"{cls} (no samples)")
            continue
        audio = extractor.load_audio(samples[0].path, training=False)
        feature = extractor.make_feature(audio, training=False)

        base = FEATURE_FRAME_COUNT * FEATURE_BIN_COUNT
        channels = len(feature) // base
        feat_3d = feature.reshape(FEATURE_FRAME_COUNT, FEATURE_BIN_COUNT, channels)
        feat_img = feat_3d[:, :, 0]

        im = ax.imshow(feat_img.T, origin="lower", aspect="auto", cmap="magma")
        ax.set_xlabel("Time frame")
        ax.set_ylabel("Mel bin")
        ax.set_title(f"{cls} ({len(samples)} samples)")
        fig.colorbar(im, ax=ax)

    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def maybe_write_confusion_plot(
    path: Path, matrix: np.ndarray, class_names: list[str],
) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is not installed; skipping confusion_matrix.png")
        return
    fig, ax = plt.subplots(
        figsize=(max(5, len(class_names)), max(4, len(class_names))),
    )
    image = ax.imshow(matrix, interpolation="nearest", cmap="Blues")
    fig.colorbar(image, ax=ax)
    ax.set_xticks(
        np.arange(len(class_names)), labels=class_names,
        rotation=45, ha="right",
    )
    ax.set_yticks(np.arange(len(class_names)), labels=class_names)
    ax.set_xlabel("Predicted")
    ax.set_ylabel("True")
    for i in range(matrix.shape[0]):
        for j in range(matrix.shape[1]):
            ax.text(j, i, str(int(matrix[i, j])), ha="center", va="center")
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


# ---------------------------------------------------------------------------
# C array export
# ---------------------------------------------------------------------------
def export_c_array(
    model_bytes: bytes,
    cc_path: Path,
    h_path: Path,
    array_name: str,
) -> None:
    guard = "AUDIO_EVENT_MODEL_DATA_H_"
    h_path.write_text(
        f"""// Generated TFLite model data.
#ifndef {guard}
#define {guard}

#include <cstdint>

extern const unsigned char {array_name}[];
extern const unsigned int {array_name}_len;

#endif  // {guard}
""",
        encoding="utf-8",
    )
    hex_values = [f"0x{byte:02x}" for byte in model_bytes]
    lines = []
    for index in range(0, len(hex_values), 12):
        lines.append("  " + ", ".join(hex_values[index:index + 12]))
    cc_path.write_text(
        f"""// Generated TFLite model data.
#include "{h_path.name}"

alignas(16) const unsigned char {array_name}[] = {{
{",\n".join(lines)}
}};

const unsigned int {array_name}_len = {len(model_bytes)};
""",
        encoding="utf-8",
    )


# ---------------------------------------------------------------------------
# JSON helpers
# ---------------------------------------------------------------------------
def write_json(path: Path, payload: dict) -> None:
    path.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def safe_div(numerator: int, denominator: int) -> float:
    if denominator == 0:
        return 0.0
    return float(numerator) / float(denominator)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main() -> None:
    args = parse_args()
    try:
        train(args)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise


if __name__ == "__main__":
    main()
