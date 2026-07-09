/*
 * Audio event detection application entry point.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "alert/event_alert.h"
#include "audio/audio_capture.h"
#include "audio/audio_file.h"
#include "audio_event_config.h"
#include "detector/event_detector.h"
#include "dsp/feature_extract.h"
#include "model/event_classifier.h"

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_UI
#include "ui/audio_event_ui.h"
#endif

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
#include "ui_oled/audio_event_oled_ui.h"
#endif

#if defined(CONFIG_EXAMPLES_AUDIO_EVENT_UI) || \
    defined(CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI)
#define AUDIO_EVENT_HAS_DISPLAY 1
#endif

#define AUDIO_IO_BLOCK_SAMPLES 512

enum input_mode_e
{
  INPUT_MODE_DEVICE = 0,
  INPUT_MODE_FILE
};

struct app_options_s
{
  enum input_mode_e mode;
  const char *path;
  unsigned int repeat_count;
  bool once;
  bool model_smoke;
  bool audio_stats;
  bool no_oled;
  bool profile;
};

static const char *const g_event_names[AUDIO_EVENT_CLASS_COUNT] =
{
  "knock",
  "cough",
  "background",
  "silence"
};

static int16_t g_audio_ring[AUDIO_EVENT_CLIP_SAMPLES];
static int16_t g_audio_window[AUDIO_EVENT_CLIP_SAMPLES];
static int16_t g_audio_block[AUDIO_IO_BLOCK_SAMPLES];
static float g_features[AUDIO_EVENT_FEATURE_SIZE];
static float g_probabilities[AUDIO_EVENT_CLASS_COUNT];

#ifdef AUDIO_EVENT_HAS_DISPLAY
static uint64_t g_cooldown_start_ms;
static uint32_t g_cooldown_duration_ms;
#endif

static uint64_t isqrt_u64(uint64_t value)
{
  uint64_t result = 0;
  uint64_t bit = (uint64_t)1 << 62;

  while (bit > value)
    {
      bit >>= 2;
    }

  while (bit != 0)
    {
      if (value >= result + bit)
        {
          value -= result + bit;
          result = (result >> 1) + bit;
        }
      else
        {
          result >>= 1;
        }

      bit >>= 2;
    }

  return result;
}

static uint64_t monotonic_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    {
      return 0;
    }

  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static uint64_t elapsed_ms(uint64_t start_ms)
{
  uint64_t now_ms = monotonic_ms();

  if (now_ms < start_ms)
    {
      return 0;
    }

  return now_ms - start_ms;
}

static uint64_t diff_ms(uint64_t start_ms, uint64_t end_ms)
{
  if (end_ms < start_ms)
    {
      return 0;
    }

  return end_ms - start_ms;
}

static void usage(const char *program)
{
  printf("Usage: %s [--file PATH | --device PATH] [--once] "
         "[--repeat N] [--model-smoke] [--audio-stats] "
         "[--no-oled] [--profile]\n", program);
  printf("  --file PATH    Read 16 kHz mono PCM16 or WAV from HostFS\n");
  printf("  --device PATH  Read from a NuttX Audio capture device\n");
  printf("  --once         Stop after the first one-second inference\n");
  printf("  --repeat N     Repeat file input N times (default: 1)\n");
  printf("  --model-smoke  Invoke the model with silence features\n");
  printf("  --audio-stats  Print PCM min/max/mean/rms/zero_count per window\n");
  printf("  --no-oled      Disable OLED init and updates for audio diagnostics\n");
  printf("  --profile      Print per-window processing time breakdown\n");
}

static int parse_options(int argc, char **argv, struct app_options_s *options)
{
  int i;

  options->mode = INPUT_MODE_DEVICE;
  options->path = CONFIG_EXAMPLES_AUDIO_EVENT_DEVPATH;
  options->repeat_count = 1;
  options->once = false;
  options->model_smoke = false;
  options->audio_stats = false;
  options->no_oled = false;
  options->profile = false;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
        {
          options->mode = INPUT_MODE_FILE;
          options->path = argv[++i];
        }
      else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc)
        {
          options->mode = INPUT_MODE_DEVICE;
          options->path = argv[++i];
        }
      else if (strcmp(argv[i], "--once") == 0)
        {
          options->once = true;
        }
      else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc)
        {
          char *end;
          unsigned long repeat = strtoul(argv[++i], &end, 10);
          if (*end != '\0' || repeat == 0 || repeat > UINT16_MAX)
            {
              return -EINVAL;
            }

          options->repeat_count = (unsigned int)repeat;
        }
      else if (strcmp(argv[i], "--model-smoke") == 0)
        {
          options->model_smoke = true;
        }
      else if (strcmp(argv[i], "--audio-stats") == 0)
        {
          options->audio_stats = true;
        }
      else if (strcmp(argv[i], "--no-oled") == 0)
        {
          options->no_oled = true;
        }
      else if (strcmp(argv[i], "--profile") == 0)
        {
          options->profile = true;
        }
      else if (strcmp(argv[i], "--help") == 0 ||
               strcmp(argv[i], "-h") == 0)
        {
          usage(argv[0]);
          return 1;
        }
      else
        {
          fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
          usage(argv[0]);
          return -EINVAL;
        }
    }

  return 0;
}

static void print_audio_stats(const int16_t *samples, size_t sample_count,
                              uint64_t timestamp_ms,
                              uint64_t wall_timestamp_ms)
{
  int16_t min_sample = 0;
  int16_t max_sample = 0;
  int64_t sum = 0;
  uint64_t sum_squares = 0;
  unsigned int zero_count = 0;
  unsigned int i;
  int64_t mean;
  uint64_t rms;

  if (samples == NULL || sample_count == 0)
    {
      return;
    }

  min_sample = samples[0];
  max_sample = samples[0];
  for (i = 0; i < sample_count; i++)
    {
      int32_t sample = samples[i];

      if (sample < min_sample)
        {
          min_sample = sample;
        }

      if (sample > max_sample)
        {
          max_sample = sample;
        }

      if (sample == 0)
        {
          zero_count++;
        }

      sum += sample;
      sum_squares += (uint64_t)((int64_t)sample * sample);
    }

  mean = sum / (int64_t)sample_count;
  rms = isqrt_u64(sum_squares / sample_count);
  printf("[audio_stats] t=%llu ms wall=%llu ms "
         "min=%d max=%d mean=%lld rms=%llu "
         "zero=%u/%u\n",
         (unsigned long long)timestamp_ms,
         (unsigned long long)wall_timestamp_ms, min_sample, max_sample,
         (long long)mean, (unsigned long long)rms, zero_count,
         (unsigned int)sample_count);
}

static uint32_t compute_audio_rms(const int16_t *samples,
                                  size_t sample_count)
{
  uint64_t sum_squares = 0;
  size_t i;

  if (samples == NULL || sample_count == 0)
    {
      return 0;
    }

  for (i = 0; i < sample_count; i++)
    {
      int32_t sample = samples[i];

      sum_squares += (uint64_t)((int64_t)sample * sample);
    }

  return (uint32_t)isqrt_u64(sum_squares / sample_count);
}

static int best_class(const float *probabilities)
{
  int best = 0;
  int i;

  for (i = 1; i < AUDIO_EVENT_CLASS_COUNT; i++)
    {
      if (probabilities[i] > probabilities[best])
        {
          best = i;
        }
    }

  return best;
}

static int probability_permille(float probability)
{
  int value = (int)(probability * 1000.0f + 0.5f);

  if (value < 0)
    {
      value = 0;
    }
  else if (value > 1000)
    {
      value = 1000;
    }

  return value;
}

static void print_probabilities(uint64_t timestamp_ms,
                                uint64_t wall_timestamp_ms)
{
  int best = best_class(g_probabilities);

  printf("[infer] t=%llu ms wall=%llu ms class=%s "
         "probs_permille=[%d %d %d %d]\n",
         (unsigned long long)timestamp_ms,
         (unsigned long long)wall_timestamp_ms, g_event_names[best],
         probability_permille(g_probabilities[0]),
         probability_permille(g_probabilities[1]),
         probability_permille(g_probabilities[2]),
         probability_permille(g_probabilities[3]));
}

static int run_model_smoke(void)
{
  int ret;

  memset(g_features, 0, sizeof(g_features));
  ret = event_classifier_predict(g_features, AUDIO_EVENT_FEATURE_SIZE,
                                 g_probabilities,
                                 AUDIO_EVENT_CLASS_COUNT);
  if (ret < 0)
    {
      return ret;
    }

  print_probabilities(0, 0);
  return 0;
}

static void ring_append(size_t *write_position, const int16_t *samples,
                        size_t sample_count)
{
  size_t i;

  for (i = 0; i < sample_count; i++)
    {
      g_audio_ring[*write_position] = samples[i];
      *write_position = (*write_position + 1) % AUDIO_EVENT_CLIP_SAMPLES;
    }
}

static void ring_copy_window(size_t write_position)
{
  size_t tail = AUDIO_EVENT_CLIP_SAMPLES - write_position;

  memcpy(g_audio_window, &g_audio_ring[write_position],
         tail * sizeof(int16_t));
  if (write_position > 0)
    {
      memcpy(&g_audio_window[tail], g_audio_ring,
             write_position * sizeof(int16_t));
    }
}

static int process_window(size_t write_position, uint64_t timestamp_ms,
                          uint64_t wall_timestamp_ms, bool audio_stats,
                          bool oled_enabled, bool profile)
{
  struct event_detection_s detection;
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
  uint32_t rms;
  int top_class;
  int top_confidence;
#endif
  int ret;
  uint64_t profile_start_ms = monotonic_ms();
  uint64_t copy_done_ms;
  uint64_t stats_done_ms;
  uint64_t feature_done_ms;
  uint64_t infer_done_ms;
  uint64_t log_done_ms;
  uint64_t ui_done_ms;
  uint64_t detector_done_ms;
  uint64_t alert_done_ms;

#ifndef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
  (void)oled_enabled;
#endif

  ring_copy_window(write_position);
  copy_done_ms = monotonic_ms();
  if (audio_stats)
    {
      print_audio_stats(g_audio_window, AUDIO_EVENT_CLIP_SAMPLES,
                        timestamp_ms, wall_timestamp_ms);
    }
  stats_done_ms = monotonic_ms();

  ret = feature_extract_compute(g_audio_window, AUDIO_EVENT_CLIP_SAMPLES,
                                g_features, AUDIO_EVENT_FEATURE_SIZE);
  if (ret < 0)
    {
      fprintf(stderr, "[feature] compute failed: %d\n", ret);
      return ret;
    }
  feature_done_ms = monotonic_ms();

  ret = event_classifier_predict(g_features, AUDIO_EVENT_FEATURE_SIZE,
                                 g_probabilities,
                                 AUDIO_EVENT_CLASS_COUNT);
  if (ret < 0)
    {
      fprintf(stderr, "[model] prediction failed: %d\n", ret);
      return ret;
    }
  infer_done_ms = monotonic_ms();

  print_probabilities(timestamp_ms, wall_timestamp_ms);
  log_done_ms = monotonic_ms();

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_UI
  audio_event_ui_update_probs(g_probabilities,
                              AUDIO_EVENT_CLASS_COUNT);
#endif

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
  if (oled_enabled)
    {
      rms = compute_audio_rms(g_audio_window, AUDIO_EVENT_CLIP_SAMPLES);
      top_class = best_class(g_probabilities);
      top_confidence = probability_permille(g_probabilities[top_class]);
      audio_event_oled_ui_update_audio(top_class, top_confidence, rms);
    }
#endif
  ui_done_ms = monotonic_ms();

  ret = event_detector_update(g_probabilities, AUDIO_EVENT_CLASS_COUNT,
                              timestamp_ms, &detection);
  if (ret < 0)
    {
      return ret;
    }
  detector_done_ms = monotonic_ms();

  if (detection.fired)
    {
      event_alert_fire(detection.class_id,
                       g_event_names[detection.class_id],
                       detection.confidence);

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_UI
      audio_event_ui_notify_detection(detection.class_id,
                                      (int)(detection.confidence *
                                            1000.0f + 0.5f));
      g_cooldown_start_ms = timestamp_ms;
      g_cooldown_duration_ms =
          (uint32_t)CONFIG_EXAMPLES_AUDIO_EVENT_COOLDOWN_MS;
      audio_event_ui_notify_cooldown(g_cooldown_duration_ms);
#endif

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
      if (oled_enabled)
        {
          audio_event_oled_ui_notify_detection(detection.class_id,
                                               (int)(detection.confidence *
                                                     1000.0f + 0.5f));
          g_cooldown_start_ms = timestamp_ms;
          g_cooldown_duration_ms =
              (uint32_t)CONFIG_EXAMPLES_AUDIO_EVENT_COOLDOWN_MS;
          audio_event_oled_ui_notify_cooldown(g_cooldown_duration_ms);
        }
#endif
    }
  alert_done_ms = monotonic_ms();

  if (profile)
    {
      printf("[profile] t=%llu ms wall=%llu ms copy=%llu stats=%llu "
             "feature=%llu infer=%llu log=%llu ui=%llu detector=%llu "
             "alert=%llu total=%llu ms\n",
             (unsigned long long)timestamp_ms,
             (unsigned long long)wall_timestamp_ms,
             (unsigned long long)diff_ms(profile_start_ms, copy_done_ms),
             (unsigned long long)diff_ms(copy_done_ms, stats_done_ms),
             (unsigned long long)diff_ms(stats_done_ms, feature_done_ms),
             (unsigned long long)diff_ms(feature_done_ms, infer_done_ms),
             (unsigned long long)diff_ms(infer_done_ms, log_done_ms),
             (unsigned long long)diff_ms(log_done_ms, ui_done_ms),
             (unsigned long long)diff_ms(ui_done_ms, detector_done_ms),
             (unsigned long long)diff_ms(detector_done_ms, alert_done_ms),
             (unsigned long long)diff_ms(profile_start_ms, alert_done_ms));
    }

  return 0;
}

int audio_event_main(int argc, char *argv[])
{
  struct app_options_s options;
  struct audio_file_s file_source;
  size_t write_position = 0;
  size_t samples_until_inference = AUDIO_EVENT_CLIP_SAMPLES;
  uint64_t total_samples = 0;
  uint64_t wall_start_ms = 0;
  unsigned int windows = 0;
  bool file_open = false;
  bool capture_open = false;
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
  bool oled_active = false;
#endif
  int ret;

  memset(&file_source, 0, sizeof(file_source));
  file_source.fd = -1;

  ret = parse_options(argc, argv, &options);
  if (ret != 0)
    {
      return ret > 0 ? 0 : EXIT_FAILURE;
    }

  printf("=== Audio Event Detection ===\n");
  ret = event_classifier_init();
  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  if (options.model_smoke)
    {
      ret = run_model_smoke();
      event_classifier_deinit();
      return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  ret = feature_extract_init();
  if (ret < 0)
    {
      event_classifier_deinit();
      return EXIT_FAILURE;
    }

  event_detector_init();
  ret = event_alert_init();
  if (ret < 0)
    {
      fprintf(stderr, "[alert] init failed: %d, serial output remains\n", ret);
    }

  wall_start_ms = monotonic_ms();

  if (options.mode == INPUT_MODE_FILE)
    {
      ret = audio_file_open(&file_source, options.path,
                            options.repeat_count);
      file_open = ret == 0;
    }
  else
    {
      ret = audio_capture_init(options.path);
      capture_open = ret == 0;
    }

  if (ret < 0)
    {
      goto cleanup;
    }

  printf("[app] source=%s hop=%d ms\n",
         options.mode == INPUT_MODE_FILE ? "file" : "device",
         CONFIG_EXAMPLES_AUDIO_EVENT_HOP_MS);

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_UI
  if (audio_event_ui_init() < 0)
    {
      fprintf(stderr, "[app] UI init failed, continuing without UI\n");
    }
#endif

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
  if (options.no_oled)
    {
      printf("[app] OLED disabled by --no-oled\n");
    }
  else if (audio_event_oled_ui_init() < 0)
    {
      fprintf(stderr, "[app] OLED UI init failed, continuing without OLED\n");
    }
  else
    {
      oled_active = true;
    }
#endif

  for (;;)
    {
      size_t request = samples_until_inference;
      ssize_t count;

      if (request > AUDIO_IO_BLOCK_SAMPLES)
        {
          request = AUDIO_IO_BLOCK_SAMPLES;
        }

      if (options.mode == INPUT_MODE_FILE)
        {
          count = audio_file_read(&file_source, g_audio_block, request);
        }
      else
        {
          count = audio_capture_read(g_audio_block, request);
        }

      if (count < 0)
        {
          ret = (int)count;
          fprintf(stderr, "[app] audio read failed: %d\n", ret);
          break;
        }

      if (count == 0)
        {
          ret = windows > 0 ? 0 : -ENODATA;
          if (ret < 0)
            {
              fprintf(stderr, "[app] input ended before one full window\n");
            }

          break;
        }

      ring_append(&write_position, g_audio_block, count);
      total_samples += count;
      samples_until_inference -= count;

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_UI
      /* Update waveform and drive LVGL event loop */
      audio_event_ui_update_waveform(g_audio_block, count);
      audio_event_ui_tick(0);
      lv_timer_handler();
#endif

      if (samples_until_inference == 0)
        {
          uint64_t timestamp_ms = total_samples * 1000 /
                                  AUDIO_EVENT_SAMPLE_RATE;
          uint64_t wall_timestamp_ms = elapsed_ms(wall_start_ms);
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
          bool oled_enabled = oled_active;
#else
          bool oled_enabled = false;
#endif

          ret = process_window(write_position, timestamp_ms,
                               wall_timestamp_ms, options.audio_stats,
                               oled_enabled, options.profile);
          if (ret < 0)
            {
              break;
            }

#ifdef AUDIO_EVENT_HAS_DISPLAY
          /* Update cooldown progress */
          if (g_cooldown_duration_ms > 0)
            {
              uint64_t elapsed = timestamp_ms - g_cooldown_start_ms;

              if (elapsed >= g_cooldown_duration_ms)
                {
                  g_cooldown_duration_ms = 0;
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_UI
                  audio_event_ui_set_listening();
#endif
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
                  if (oled_active)
                    {
                      audio_event_oled_ui_set_listening();
                    }
#endif
                }
              else
                {
                  uint32_t remaining =
                      (uint32_t)(g_cooldown_duration_ms - elapsed);
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_UI
                  audio_event_ui_update_cooldown(remaining);
#endif
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
                  if (oled_active)
                    {
                      audio_event_oled_ui_update_cooldown(remaining);
                    }
#endif
                }
            }
#endif

          windows++;
          if (options.once)
            {
              ret = 0;
              break;
            }

          samples_until_inference =
              AUDIO_EVENT_SAMPLE_RATE *
              CONFIG_EXAMPLES_AUDIO_EVENT_HOP_MS / 1000;
          if (samples_until_inference == 0 ||
              samples_until_inference > AUDIO_EVENT_CLIP_SAMPLES)
            {
              fprintf(stderr, "[app] invalid hop configuration\n");
              ret = -EINVAL;
              break;
            }
        }
    }

cleanup:
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_UI
  audio_event_ui_deinit();
#endif

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI
  if (oled_active)
    {
      audio_event_oled_ui_deinit();
    }
#endif

  if (file_open)
    {
      audio_file_close(&file_source);
    }

  if (capture_open)
    {
      audio_capture_deinit();
    }

  event_alert_deinit();
  feature_extract_deinit();
  event_classifier_deinit();
  printf("[app] stopped, windows=%u status=%d\n", windows, ret);
  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
