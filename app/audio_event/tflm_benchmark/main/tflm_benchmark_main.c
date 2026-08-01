/*
 * Standalone operator profiler for the production audio_event model.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_event_config.h"
#include "model/event_classifier.h"

struct benchmark_options_s
{
  unsigned int warmup_count;
  unsigned int repeat_count;
  bool csv;
};

static float g_smoke_features[AUDIO_EVENT_FEATURE_SIZE];

static void usage(const char *program)
{
  printf("Usage: %s [--warmup N] [--repeat N] [--csv]\n", program);
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

  memset(g_smoke_features, 0, sizeof(g_smoke_features));
  printf("[tflm_benchmark] model_input=float_zero feature_count=%u "
         "arena_used=%lu\n", AUDIO_EVENT_FEATURE_SIZE,
         (unsigned long)event_classifier_arena_used());
  ret = event_classifier_profile(g_smoke_features, AUDIO_EVENT_FEATURE_SIZE,
                                 options.warmup_count, options.repeat_count,
                                 options.csv);
  event_classifier_deinit();
  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
