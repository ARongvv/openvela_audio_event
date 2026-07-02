/**
 * apps/examples/audio_event/event_alert.c
 *
 * Alert output: serial log (always), LED / buzzer (conditional).
 */

#include "alert/event_alert.h"

#include <stdio.h>

int event_alert_init(void)
{
    /* TODO: open /dev/userleds or /dev/pwm0 for buzzer */

    printf("[event_alert] init OK\n");
    return 0;
}

void event_alert_deinit(void)
{
    /* TODO: close devices */
}

void event_alert_fire(int event_id, const char *event_name,
                      float confidence)
{
    int confidence_permille = (int)(confidence * 1000.0f + 0.5f);

    (void)event_id;

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
}
