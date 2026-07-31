/*
 * Stateful conversion from window probabilities to alert events.
 */

#ifndef __APPS_EXAMPLES_AUDIO_EVENT_DETECTOR_EVENT_DETECTOR_H
#define __APPS_EXAMPLES_AUDIO_EVENT_DETECTOR_EVENT_DETECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct event_detection_s
{
  bool fired;
  int class_id;
  float confidence;
};

void event_detector_init(void);
void event_detector_reset_candidate(void);
int event_detector_update(const float *probabilities, size_t class_count,
                          uint64_t timestamp_ms,
                          struct event_detection_s *detection);

#endif /* __APPS_EXAMPLES_AUDIO_EVENT_DETECTOR_EVENT_DETECTOR_H */
