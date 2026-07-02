/*
 * Probability thresholding, consecutive confirmation and cooldown.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>

#include "audio_event_config.h"
#include "detector/event_detector.h"

static int g_candidate = -1;
static int g_hits;
static uint64_t g_cooldown_until_ms;

void event_detector_init(void)
{
  g_candidate = -1;
  g_hits = 0;
  g_cooldown_until_ms = 0;
}

int event_detector_update(const float *probabilities, size_t class_count,
                          uint64_t timestamp_ms,
                          struct event_detection_s *detection)
{
  float thresholds[2] =
    {
      CONFIG_EXAMPLES_AUDIO_EVENT_KNOCK_THRESHOLD / 1000.0f,
      CONFIG_EXAMPLES_AUDIO_EVENT_COUGH_THRESHOLD / 1000.0f
    };
  int best = 0;
  size_t i;

  if (probabilities == NULL || detection == NULL ||
      class_count != AUDIO_EVENT_CLASS_COUNT)
    {
      return -EINVAL;
    }

  detection->fired = false;
  detection->class_id = -1;
  detection->confidence = 0.0f;

  for (i = 1; i < class_count; i++)
    {
      if (probabilities[i] > probabilities[best])
        {
          best = i;
        }
    }

  if (timestamp_ms < g_cooldown_until_ms ||
      best > AUDIO_EVENT_CLASS_COUGH || probabilities[best] < thresholds[best])
    {
      g_candidate = -1;
      g_hits = 0;
      return 0;
    }

  if (best == g_candidate)
    {
      g_hits++;
    }
  else
    {
      g_candidate = best;
      g_hits = 1;
    }

  if (g_hits >= CONFIG_EXAMPLES_AUDIO_EVENT_CONSECUTIVE_HITS)
    {
      detection->fired = true;
      detection->class_id = best;
      detection->confidence = probabilities[best];
      g_cooldown_until_ms = timestamp_ms +
                            CONFIG_EXAMPLES_AUDIO_EVENT_COOLDOWN_MS;
      g_candidate = -1;
      g_hits = 0;
    }

  return 0;
}
