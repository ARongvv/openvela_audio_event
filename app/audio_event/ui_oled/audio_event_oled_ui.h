/*
 * Compact monochrome OLED UI for audio_event.
 */

#ifndef __APPS_EXAMPLES_AUDIO_EVENT_UI_OLED_AUDIO_EVENT_OLED_UI_H
#define __APPS_EXAMPLES_AUDIO_EVENT_UI_OLED_AUDIO_EVENT_OLED_UI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int audio_event_oled_ui_init(void);
void audio_event_oled_ui_deinit(void);
bool audio_event_oled_ui_is_active(void);

void audio_event_oled_ui_update_audio(int class_id, int confidence,
                                      uint32_t rms);
void audio_event_oled_ui_notify_detection(int class_id, int confidence);
void audio_event_oled_ui_notify_cooldown(uint32_t duration_ms);
void audio_event_oled_ui_update_cooldown(uint32_t remaining_ms);
void audio_event_oled_ui_set_listening(void);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_EXAMPLES_AUDIO_EVENT_UI_OLED_AUDIO_EVENT_OLED_UI_H */
