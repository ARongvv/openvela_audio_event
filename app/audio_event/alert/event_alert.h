/**
 * apps/examples/audio_event/event_alert.h
 *
 * Alert output interface: LED, buzzer, serial log, optional LVGL display.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  event_alert_init(void);
void event_alert_deinit(void);

/**
 * Fire an alert for the detected event.
 *
 * @param event_id    Numeric event class ID.
 * @param event_name  Human-readable event name.
 * @param confidence  Prediction confidence [0.0, 1.0].
 */
void event_alert_fire(int event_id, const char *event_name,
                      float confidence);

/**
 * Fire a local alert and enqueue a remote report with the audio timestamp.
 * Network transfer is always performed outside this caller's execution path.
 */
void event_alert_fire_at(int event_id, const char *event_name,
                         float confidence, uint64_t monotonic_ms);

#ifdef __cplusplus
}
#endif
