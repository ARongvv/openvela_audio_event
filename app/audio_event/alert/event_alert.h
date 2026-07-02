/**
 * apps/examples/audio_event/event_alert.h
 *
 * Alert output interface: LED, buzzer, serial log, optional LVGL display.
 */

#pragma once

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

#ifdef __cplusplus
}
#endif
