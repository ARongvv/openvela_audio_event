/*
 * TensorFlow-compatible 49 x 40 x 3 log-mel frontend.
 */

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <tools/kiss_fftr.h>

#include "audio_event_config.h"
#include "dsp/feature_extract.h"

#define MEL_LOWER_HZ        125.0f
#define MEL_UPPER_HZ       7500.0f
#define LOG_EPSILON           1.0e-6f
#define FEATURE_OFFSET       12.0f
#define FEATURE_SCALE         1.625f
#define FEATURE_MIN           0.0f
#define FEATURE_MAX          26.0f
#define FFT_STATE_BYTES    8192
#define PI_F                   3.14159265358979323846f

static uint8_t g_fft_state[FFT_STATE_BYTES] __attribute__((aligned(16)));
static kiss_fftr_cfg g_fft;
static float g_hann[AUDIO_EVENT_WINDOW_SAMPLES];
static float g_fft_input[AUDIO_EVENT_FFT_SIZE];
static kiss_fft_cpx g_fft_output[AUDIO_EVENT_FFT_SIZE / 2 + 1];
static float g_bin_mel[AUDIO_EVENT_FFT_SIZE / 2 + 1];
static float g_band_edge_mel[AUDIO_EVENT_FEATURE_BINS + 2];

static float hertz_to_mel(float hertz)
{
  return 1127.0f * logf(1.0f + hertz / 700.0f);
}

static size_t feature_index(size_t frame, size_t band, size_t channel)
{
  return ((frame * AUDIO_EVENT_FEATURE_BINS + band) *
          AUDIO_EVENT_FEATURE_CHANNELS) + channel;
}

static size_t clamp_frame(int frame)
{
  if (frame < 0)
    {
      return 0;
    }

  if (frame >= AUDIO_EVENT_FEATURE_FRAMES)
    {
      return AUDIO_EVENT_FEATURE_FRAMES - 1;
    }

  return (size_t)frame;
}

static float temporal_delta(const float *features, size_t frame, size_t band,
                            size_t channel)
{
  float numerator = 0.0f;
  int n;

  for (n = 1; n <= 2; n++)
    {
      size_t prev = clamp_frame((int)frame - n);
      size_t next = clamp_frame((int)frame + n);

      numerator += (float)n *
                   (features[feature_index(next, band, channel)] -
                    features[feature_index(prev, band, channel)]);
    }

  return numerator / 10.0f;
}

int feature_extract_init(void)
{
  size_t fft_state_size = sizeof(g_fft_state);
  float lower_mel = hertz_to_mel(MEL_LOWER_HZ);
  float upper_mel = hertz_to_mel(MEL_UPPER_HZ);
  size_t i;

  g_fft = kiss_fftr_alloc(AUDIO_EVENT_FFT_SIZE, 0, g_fft_state,
                          &fft_state_size);
  if (g_fft == NULL)
    {
      fprintf(stderr, "[feature] FFT state needs %zu bytes, have %zu\n",
              fft_state_size, sizeof(g_fft_state));
      return -ENOMEM;
    }

  /* tf.signal.hann_window() defaults to a periodic window. */

  for (i = 0; i < AUDIO_EVENT_WINDOW_SAMPLES; i++)
    {
      g_hann[i] = 0.5f -
                  0.5f * cosf(2.0f * PI_F * (float)i /
                              (float)AUDIO_EVENT_WINDOW_SAMPLES);
    }

  for (i = 0; i <= AUDIO_EVENT_FFT_SIZE / 2; i++)
    {
      float hertz = (float)i * AUDIO_EVENT_SAMPLE_RATE /
                    AUDIO_EVENT_FFT_SIZE;
      g_bin_mel[i] = hertz_to_mel(hertz);
    }

  for (i = 0; i < AUDIO_EVENT_FEATURE_BINS + 2; i++)
    {
      g_band_edge_mel[i] =
          lower_mel + (upper_mel - lower_mel) * (float)i /
                      (AUDIO_EVENT_FEATURE_BINS + 1);
    }

  printf("[feature] log-mel+delta ready: frames=%d bins=%d channels=%d "
         "fft=%d\n",
         AUDIO_EVENT_FEATURE_FRAMES, AUDIO_EVENT_FEATURE_BINS,
         AUDIO_EVENT_FEATURE_CHANNELS, AUDIO_EVENT_FFT_SIZE);
  return 0;
}

void feature_extract_deinit(void)
{
  g_fft = NULL;
}

int feature_extract_compute(const int16_t *samples, size_t sample_count,
                            float *features, size_t feature_count)
{
  size_t frame;

  if (g_fft == NULL || samples == NULL || features == NULL ||
      sample_count != AUDIO_EVENT_CLIP_SAMPLES ||
      feature_count != AUDIO_EVENT_FEATURE_SIZE)
    {
      return -EINVAL;
    }

  for (frame = 0; frame < AUDIO_EVENT_FEATURE_FRAMES; frame++)
    {
      size_t frame_offset = frame * AUDIO_EVENT_WINDOW_STEP_SAMPLES;
      size_t i;
      int band;

      memset(g_fft_input, 0, sizeof(g_fft_input));
      for (i = 0; i < AUDIO_EVENT_WINDOW_SAMPLES; i++)
        {
          float audio = samples[frame_offset + i] / 32768.0f;
          g_fft_input[i] = audio * g_hann[i];
        }

      kiss_fftr(g_fft, g_fft_input, g_fft_output);

      for (band = 0; band < AUDIO_EVENT_FEATURE_BINS; band++)
        {
          float lower = g_band_edge_mel[band];
          float center = g_band_edge_mel[band + 1];
          float upper = g_band_edge_mel[band + 2];
          float mel_energy = 0.0f;
          size_t bin;

          /* TensorFlow explicitly zeros the DC row of the Mel matrix. */

          for (bin = 1; bin <= AUDIO_EVENT_FFT_SIZE / 2; bin++)
            {
              float mel = g_bin_mel[bin];
              float weight;

              if (mel <= lower || mel >= upper)
                {
                  continue;
                }

              if (mel <= center)
                {
                  weight = (mel - lower) / (center - lower);
                }
              else
                {
                  weight = (upper - mel) / (upper - center);
                }

              if (weight > 0.0f)
                {
                  float real = g_fft_output[bin].r;
                  float imag = g_fft_output[bin].i;
                  float magnitude = sqrtf(real * real + imag * imag);
                  mel_energy += magnitude * weight;
                }
            }

          float value = (logf(mel_energy + LOG_EPSILON) + FEATURE_OFFSET) *
                        FEATURE_SCALE;
          if (value < FEATURE_MIN)
            {
              value = FEATURE_MIN;
            }
          else if (value > FEATURE_MAX)
            {
              value = FEATURE_MAX;
            }

          features[feature_index(frame, band, 0)] = value;
        }
    }

  for (frame = 0; frame < AUDIO_EVENT_FEATURE_FRAMES; frame++)
    {
      int band;

      for (band = 0; band < AUDIO_EVENT_FEATURE_BINS; band++)
        {
          features[feature_index(frame, band, 1)] =
              temporal_delta(features, frame, band, 0);
        }
    }

  for (frame = 0; frame < AUDIO_EVENT_FEATURE_FRAMES; frame++)
    {
      int band;

      for (band = 0; band < AUDIO_EVENT_FEATURE_BINS; band++)
        {
          features[feature_index(frame, band, 2)] =
              temporal_delta(features, frame, band, 1);
        }
    }

  return 0;
}
