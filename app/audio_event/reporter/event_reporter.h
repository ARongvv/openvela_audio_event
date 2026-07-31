/*
 * Asynchronous remote event reporter for the audio event application.
 */

#ifndef __APPS_EXAMPLES_AUDIO_EVENT_REPORTER_EVENT_REPORTER_H
#define __APPS_EXAMPLES_AUDIO_EVENT_REPORTER_EVENT_REPORTER_H

#include <stdbool.h>
#include <stdint.h>

#define EVENT_REPORTER_EVENT_NAME_MAX 16

/*
 * A value object kept in the fixed-size RAM queue.  event_name is copied
 * when an alert is enqueued, so it never references classifier-owned data.
 */

struct report_event_s
{
  uint32_t sequence;
  uint32_t boot_id;
  uint64_t monotonic_ms;
  int event_class_id;
  uint16_t confidence_permille;
  char event_name[EVENT_REPORTER_EVENT_NAME_MAX];
};

struct event_reporter_stats_s
{
  uint32_t queued_events;
  uint32_t queue_high_water;
  uint32_t sent_events;
  uint32_t failed_attempts;
  uint32_t retries;
  uint32_t dropped_events;
  int last_error;
  unsigned int last_http_status;
  bool enabled;
};

/* Start or stop the optional background reporting worker. */

int event_reporter_init(void);
void event_reporter_deinit(void);

/*
 * Copy a locally confirmed event into the bounded reporting queue.
 *
 * The function never performs network I/O.  A full queue returns -ENOSPC;
 * callers must keep local alerting independent from this return value.
 */

int event_reporter_enqueue(int event_class_id, const char *event_name,
                           uint16_t confidence_permille,
                           uint64_t monotonic_ms);

/* Copy a consistent reporter statistics snapshot. */

void event_reporter_get_stats(struct event_reporter_stats_s *stats);

/* Internal HTTP backend used only by event_reporter.c. */

int event_reporter_http_post(const struct report_event_s *event,
                             unsigned int *http_status);

#endif /* __APPS_EXAMPLES_AUDIO_EVENT_REPORTER_EVENT_REPORTER_H */
