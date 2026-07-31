/**
 * apps/examples/audio_event/event_alert.c
 *
 * Alert output: serial log (always), LED / buzzer (conditional).
 */

#include "alert/event_alert.h"

#include "reporter/event_reporter.h"

#include <stdio.h>

int event_alert_init(void)
{
    int ret;

    /* TODO: open /dev/userleds or /dev/pwm0 for buzzer */

    ret = event_reporter_init();
    if (ret < 0)
      {
        /* Remote delivery is optional; serial/OLED alerting remains usable. */

        fprintf(stderr, "[event_alert] reporter unavailable: %d\n", ret);
      }

    printf("[event_alert] init OK\n");
    return 0;
}

void event_alert_deinit(void)
{
    struct event_reporter_stats_s reporter_stats;

    /* TODO: close devices */

    event_reporter_get_stats(&reporter_stats);
    if (reporter_stats.enabled)
      {
        printf("[reporter] queued=%lu high_water=%lu sent=%lu "
               "failed=%lu retries=%lu dropped=%lu last_error=%d "
               "http_status=%u\n",
               (unsigned long)reporter_stats.queued_events,
               (unsigned long)reporter_stats.queue_high_water,
               (unsigned long)reporter_stats.sent_events,
               (unsigned long)reporter_stats.failed_attempts,
               (unsigned long)reporter_stats.retries,
               (unsigned long)reporter_stats.dropped_events,
               reporter_stats.last_error, reporter_stats.last_http_status);
      }

    event_reporter_deinit();
}

void event_alert_fire(int event_id, const char *event_name,
                      float confidence)
{
    event_alert_fire_at(event_id, event_name, confidence, 0);
}

void event_alert_fire_at(int event_id, const char *event_name,
                         float confidence, uint64_t monotonic_ms)
{
    int confidence_permille = (int)(confidence * 1000.0f + 0.5f);

    if (confidence_permille < 0)
      {
        confidence_permille = 0;
      }
    else if (confidence_permille > 1000)
      {
        confidence_permille = 1000;
      }

    /*
     * Always print to serial — minimal viable alert.
     * TODO: blink LED, beep buzzer, push to LVGL UI.
     */
    printf("[ALERT] *** %s detected *** (confidence=%d/1000)\n",
           event_name ? event_name : "?",
           confidence_permille);

    /* Queue insertion is bounded and never performs DNS or socket I/O. */

    (void)event_reporter_enqueue(event_id, event_name,
                                 (uint16_t)confidence_permille,
                                 monotonic_ms);
}
