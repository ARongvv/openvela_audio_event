/*
 * Standalone operator profiler for the production audio_event model.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_event_config.h"
#include "model/event_classifier.h"

#define INVOKE_BENCHMARK_MAX_SAMPLES 1024

struct benchmark_options_s
{
  unsigned int warmup_count;
  unsigned int repeat_count;
  bool csv;
  bool invoke_mode;
  bool pattern_input;
};

static float g_smoke_features[AUDIO_EVENT_FEATURE_SIZE];
static int8_t g_benchmark_features[AUDIO_EVENT_FEATURE_SIZE];
static uint32_t g_invoke_cycles[INVOKE_BENCHMARK_MAX_SAMPLES];

static void usage(const char *program)
{
  printf("Usage: %s [--mode operator|invoke] [--input zero|pattern] "
         "[--warmup N] [--repeat N] [--csv]\n", program);
  printf("  --mode MODE  operator (default) or precise Invoke CCOUNT timing\n");
  printf("  --input SRC  zero (default) or deterministic non-zero pattern\n");
  printf("  --warmup N  Invoke without recording first N times (default: 5)\n");
  printf("  --repeat N  Number of recorded invocations (default: 30)\n");
  printf("  --csv       Emit per-operator CSV records for host aggregation\n");
}

static int parse_count(const char *text, bool allow_zero, unsigned int *value)
{
  char *end;
  unsigned long parsed;

  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed > UINT16_MAX ||
      (!allow_zero && parsed == 0))
    {
      return -EINVAL;
    }

  *value = (unsigned int)parsed;
  return 0;
}

static int parse_options(int argc, char **argv,
                         struct benchmark_options_s *options)
{
  int i;

  options->warmup_count = 5;
  options->repeat_count = 30;
  options->csv = false;
  options->invoke_mode = false;
  options->pattern_input = false;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
        {
          if (parse_count(argv[++i], true, &options->warmup_count) < 0)
            {
              return -EINVAL;
            }
        }
      else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc)
        {
          if (parse_count(argv[++i], false, &options->repeat_count) < 0)
            {
              return -EINVAL;
            }
        }
      else if (strcmp(argv[i], "--csv") == 0)
        {
          options->csv = true;
        }
      else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc)
        {
          const char *mode = argv[++i];

          if (strcmp(mode, "operator") == 0)
            {
              options->invoke_mode = false;
            }
          else if (strcmp(mode, "invoke") == 0)
            {
              options->invoke_mode = true;
            }
          else
            {
              return -EINVAL;
            }
        }
      else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc)
        {
          const char *input = argv[++i];

          if (strcmp(input, "zero") == 0)
            {
              options->pattern_input = false;
            }
          else if (strcmp(input, "pattern") == 0)
            {
              options->pattern_input = true;
            }
          else
            {
              return -EINVAL;
            }
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

static int compare_u32(const void *left, const void *right)
{
  uint32_t a = *(const uint32_t *)left;
  uint32_t b = *(const uint32_t *)right;

  return (a > b) - (a < b);
}

static uint32_t output_hash(const int8_t *output, size_t count)
{
  uint32_t hash = 2166136261u;
  size_t i;

  for (i = 0; i < count; i++)
    {
      hash ^= (uint8_t)output[i];
      hash *= 16777619u;
    }

  return hash;
}

static void fill_benchmark_features(bool pattern)
{
  uint32_t state = 0x5a17c3e1u;
  size_t i;

  for (i = 0; i < AUDIO_EVENT_FEATURE_SIZE; i++)
    {
      if (!pattern)
        {
          g_benchmark_features[i] = 0;
          continue;
        }

      state = state * 1664525u + 1013904223u;
      g_benchmark_features[i] = (int8_t)(state >> 24);
    }
}

static int run_invoke_benchmark(const struct benchmark_options_s *options)
{
  int8_t output[AUDIO_EVENT_CLASS_COUNT];
  uint32_t first_hash = 0;
  uint32_t tick_hz;
  uint64_t total_cycles = 0;
  unsigned int run;
  int ret;

  tick_hz = event_classifier_benchmark_ticks_per_second();
  if (tick_hz == 0)
    {
      fprintf(stderr,
              "[tflm_benchmark] Invoke CCOUNT timing unavailable: enable "
              "CONFIG_TFLITEMICRO_ESP32S3_CCOUNT_PROFILER\n");
      return -ENOTSUP;
    }

  if (options->repeat_count > INVOKE_BENCHMARK_MAX_SAMPLES)
    {
      fprintf(stderr,
              "[tflm_benchmark] --repeat exceeds Invoke sample limit (%u)\n",
              INVOKE_BENCHMARK_MAX_SAMPLES);
      return -EINVAL;
    }

  fill_benchmark_features(options->pattern_input);
  for (run = 0; run < options->warmup_count; run++)
    {
      uint32_t cycles;

      ret = event_classifier_benchmark_invoke_quantized(
          g_benchmark_features, AUDIO_EVENT_FEATURE_SIZE, &cycles, output,
          AUDIO_EVENT_CLASS_COUNT);
      if (ret < 0)
        {
          return ret;
        }
    }

  for (run = 0; run < options->repeat_count; run++)
    {
      uint32_t cycles;
      uint32_t hash;

      ret = event_classifier_benchmark_invoke_quantized(
          g_benchmark_features, AUDIO_EVENT_FEATURE_SIZE, &cycles, output,
          AUDIO_EVENT_CLASS_COUNT);
      if (ret < 0)
        {
          return ret;
        }

      hash = output_hash(output, AUDIO_EVENT_CLASS_COUNT);
      if (run == 0)
        {
          first_hash = hash;
        }
      else if (hash != first_hash)
        {
          fprintf(stderr,
                  "[tflm_benchmark] output changed at iteration %u: "
                  "0x%08" PRIx32 " != 0x%08" PRIx32 "\n",
                  run + 1, hash, first_hash);
          return -EIO;
        }

      g_invoke_cycles[run] = cycles;
      total_cycles += cycles;
    }

  qsort(g_invoke_cycles, options->repeat_count, sizeof(g_invoke_cycles[0]),
        compare_u32);
  printf("[tflm_benchmark] mode=invoke input=%s warmup=%u repeat=%u "
         "ccount_hz=%" PRIu32 "\n",
         options->pattern_input ? "pattern" : "zero", options->warmup_count,
         options->repeat_count, tick_hz);
  printf("[tflm_benchmark] invoke_cycles min=%" PRIu32 " p50=%" PRIu32
         " mean=%" PRIu64 " p95=%" PRIu32 " max=%" PRIu32 "\n",
         g_invoke_cycles[0],
         g_invoke_cycles[(options->repeat_count - 1) / 2],
         total_cycles / options->repeat_count,
         g_invoke_cycles[(options->repeat_count * 95 + 99) / 100 - 1],
         g_invoke_cycles[options->repeat_count - 1]);
  printf("[tflm_benchmark] invoke_us min=%" PRIu64 " p50=%" PRIu64
         " mean=%" PRIu64 " p95=%" PRIu64 " max=%" PRIu64
         " output_hash=0x%08" PRIx32 "\n",
         (uint64_t)g_invoke_cycles[0] * 1000000u / tick_hz,
         (uint64_t)g_invoke_cycles[(options->repeat_count - 1) / 2] *
             1000000u / tick_hz,
         (total_cycles / options->repeat_count) * 1000000u / tick_hz,
         (uint64_t)g_invoke_cycles[(options->repeat_count * 95 + 99) / 100 -
                                    1] *
             1000000u / tick_hz,
         (uint64_t)g_invoke_cycles[options->repeat_count - 1] * 1000000u /
             tick_hz,
         first_hash);
  return 0;
}

int tflm_benchmark_main(int argc, char **argv)
{
  struct benchmark_options_s options;
  int ret;

  ret = parse_options(argc, argv, &options);
  if (ret != 0)
    {
      return ret > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  ret = event_classifier_init();
  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  printf("[tflm_benchmark] feature_count=%u arena_used=%lu\n",
         AUDIO_EVENT_FEATURE_SIZE,
         (unsigned long)event_classifier_arena_used());
  if (options.invoke_mode)
    {
      ret = run_invoke_benchmark(&options);
    }
  else
    {
      if (options.pattern_input)
        {
          fprintf(stderr,
                  "[tflm_benchmark] --input pattern requires --mode invoke\n");
          ret = -EINVAL;
        }
      else
        {
          memset(g_smoke_features, 0, sizeof(g_smoke_features));
          ret = event_classifier_profile(
              g_smoke_features, AUDIO_EVENT_FEATURE_SIZE,
              options.warmup_count, options.repeat_count, options.csv);
        }
    }
  event_classifier_deinit();
  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
