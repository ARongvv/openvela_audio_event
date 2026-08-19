# Third-Party Notices

This file summarizes the third-party software and dataset sources relevant to
`ccf_audioevent`. It is a practical compliance aid, not a replacement for the
full upstream license texts.

## Project License

Project-authored `ccf_audioevent` source code and documentation are released
under the Apache License, Version 2.0. See `LICENSE` and `NOTICE`.

This license change applies only to project-authored material for which the
project has authority to grant the license. Vendored or copied upstream code,
linked openvela components, datasets, audio assets, and model artifacts retain
their original licenses, copyright notices, and use restrictions.

## Runtime and Build Dependencies

The project is designed to be linked into an openvela checkout through
workspace symlinks. Most runtime dependencies are provided by openvela/NuttX and
are not vendored directly in this repository.

| Component | Use in this project | License / notice source |
| --- | --- | --- |
| openvela / NuttX | RTOS, build system, audio, I2C, LCD, board integration | See `nuttx/LICENSE`, `nuttx/NOTICE`, `apps/LICENSE`, `apps/NOTICE`, and vendor notices in the openvela tree |
| TensorFlow Lite Micro | int8 model inference | Apache License 2.0; openvela path `apps/mlearning/tflite-micro/` |
| ESP-NN | ESP32-S3 optimized int8 TFLite Micro kernels under evaluation | Apache License 2.0; vendored at `third_party/esp-nn/`; source revision and import notes are in `third_party/esp-nn/UPSTREAM.md` |
| FlatBuffers | TFLite model schema/runtime support | Apache License 2.0; openvela path `apps/system/flatbuffers/` |
| KissFFT | FFT used by the audio feature extractor | BSD-style license from upstream KissFFT; openvela path `apps/math/kissfft/` |
| gemmlowp | quantized math support used by TFLite Micro | Apache License 2.0; openvela path `apps/math/gemmlowp/` |
| ruy | matrix multiplication support used by TFLite Micro | Apache License 2.0; openvela path `apps/math/ruy/` |
| LVGL | simulator dashboard UI when enabled | MIT License upstream; openvela path `apps/graphics/lvgl/` |
| Espressif ESP32-S3 HAL/board support | ESP32-S3 DevKit bring-up and peripheral support | See `vendor/espressif/LICENSE` and fetched `esp-hal-3rdparty` license files in the openvela tree |

## Dataset and Model Sources

The deployed model is trained for four labels:

```text
knock, cough, background, silence
```

The training data source mix is:

| Source | Role | License notes |
| --- | --- | --- |
| FSD50K | Source material for event/background audio | FSD50K audio clips use mixed Creative Commons licenses, including CC0, CC-BY, CC-BY-NC, and CC Sampling+. The dataset entity is released under CC-BY. Per-clip license and attribution metadata must be preserved when raw clips are redistributed. Commercial reuse needs separate review because some clips are non-commercial or Sampling+. |
| ESC-50 | Source material for environmental events, including cough/door knock related classes | ESC-50 as a whole is licensed under Creative Commons Attribution-NonCommercial 3.0; the ESC-10 subset is licensed under Creative Commons Attribution 3.0. |
| Self-collected recordings | Real-device samples collected with ESP32-S3 DevKit + INMP441 and local environment/background samples | Project-owned data unless otherwise stated. Raw recordings may contain environmental or personal audio and should be reviewed before redistribution. |

The repository stores trained model artifacts and metrics, not the full
upstream raw datasets. If raw FSD50K or ESC-50 clips are added later, keep their
original attribution files and license metadata with the redistributed files.

## Data Compliance Notes

- The model is intended for contest, research, and demonstration use.
- Because ESC-50 and parts of FSD50K include non-commercial terms, commercial
  use of models trained from those materials should be reviewed separately.
- When publishing demos, avoid exposing private conversations or identifiable
  personal audio in self-collected recordings.
- Dataset provenance for each released model should be recorded in
  `app/audio_event/model/metadata.json` or an accompanying model card.

## Upstream References

- FSD50K Zenodo record: https://zenodo.org/records/4060432
- FSD50K release page: https://fsannotator.upf.edu/fsd/release/FSD50K/
- ESC-50 repository and license: https://github.com/karolpiczak/ESC-50
- TensorFlow Lite Micro: https://github.com/tensorflow/tflite-micro
- ESP-NN: https://github.com/espressif/esp-nn
- FlatBuffers: https://github.com/google/flatbuffers
- KissFFT: https://github.com/mborgerding/kissfft
- LVGL: https://github.com/lvgl/lvgl
