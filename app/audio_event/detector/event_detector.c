/*
 * Probability thresholding, consecutive confirmation and cooldown.
 *
 * Two class groups are handled:
 *   - event classes (knock/cough/...): require a per-class threshold AND
 *     consecutive hits; fire an alert with the configured cooldown.
 *   - keyword classes (yes/no/stop, 8-class model only): use a single
 *     keyword threshold; a single clear window above threshold fires.
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
  event_detector_reset_candidate();
  g_cooldown_until_ms = 0;
}

void event_detector_reset_candidate(void)
{
  g_candidate = -1;
  g_hits = 0;
}

static float event_threshold(int class_id)
{
  switch (class_id)
    {
    case AUDIO_EVENT_CLASS_KNOCK:
      return CONFIG_EXAMPLES_AUDIO_EVENT_KNOCK_THRESHOLD / 1000.0f;

    case AUDIO_EVENT_CLASS_COUGH:
      return CONFIG_EXAMPLES_AUDIO_EVENT_COUGH_THRESHOLD / 1000.0f;

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_MODEL_8CLASS
    case AUDIO_EVENT_CLASS_GLASS_BREAKING:
      return CONFIG_EXAMPLES_AUDIO_EVENT_GLASS_THRESHOLD / 1000.0f;
#endif

    default:
      return 1.0f; /* never reachable for event classes */
    }
}

int event_detector_update(const float *probabilities, size_t class_count,
                          uint64_t timestamp_ms,
                          struct event_detection_s *detection)
{
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

  if (timestamp_ms < g_cooldown_until_ms)
    {
      g_candidate = -1;
      g_hits = 0;
      return 0;
    }

  /* Classes at/after AUDIO_EVENT_CLASS_BACKGROUND never trigger. */

  if (best >= AUDIO_EVENT_CLASS_BACKGROUND)
    {
      g_candidate = -1;
      g_hits = 0;
      return 0;
    }

  if (best < AUDIO_EVENT_EVENT_CLASS_COUNT)
    {
      /* Event class: per-class threshold + consecutive hits. */

      float threshold = event_threshold(best);

      if (probabilities[best] < threshold)
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
    }
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_MODEL_8CLASS
  else
    {
      /* Keyword class (yes/no/stop): single-window trigger. */

      if (probabilities[best] <
          CONFIG_EXAMPLES_AUDIO_EVENT_KEYWORD_THRESHOLD / 1000.0f)
        {
          g_candidate = -1;
          g_hits = 0;
          return 0;
        }

      detection->fired = true;
      detection->class_id = best;
      detection->confidence = probabilities[best];
      g_cooldown_until_ms = timestamp_ms +
                            CONFIG_EXAMPLES_AUDIO_EVENT_COOLDOWN_MS;
      g_candidate = -1;
      g_hits = 0;
    }
#endif

  return 0;
}
