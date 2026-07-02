/**
 * apps/examples/audio_event/ui/audio_event_ui.h
 *
 * Public interface for the audio event detector LVGL dashboard UI.
 */

#pragma once

#include <lvgl/lvgl.h>

#include "audio_event_config.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── UI state structure ──────────────────────────────────────── */

typedef struct
{
  /* Screens */
  lv_obj_t *screen;
  lv_obj_t *waveform_card;
  lv_obj_t *prob_card;
  lv_obj_t *detection_card;

  /* Status bar */
  lv_obj_t *status_bar;
  lv_obj_t *status_led;
  lv_obj_t *status_title;
  lv_obj_t *status_state;
  lv_obj_t *status_clock;

  /* Waveform */
  lv_obj_t *waveform_container;
  lv_obj_t *waveform_canvas;
  lv_color_t *canvas_buf;

  /* Probability bars */
  lv_obj_t *prob_container;
  lv_obj_t *prob_bars[AUDIO_EVENT_CLASS_COUNT];
  lv_obj_t *prob_labels[AUDIO_EVENT_CLASS_COUNT];
  lv_obj_t *prob_values[AUDIO_EVENT_CLASS_COUNT];

  /* Detection status */
  lv_obj_t *detection_container;
  lv_obj_t *detection_label;
  lv_obj_t *detection_confidence;
  lv_obj_t *cooldown_bar;
  lv_obj_t *cooldown_label;

  /* Bottom params bar */
  lv_obj_t *params_bar;
  lv_obj_t *param_threshold;
  lv_obj_t *param_consecutive;
  lv_obj_t *param_cooldown;

  /* Animation state */
  lv_anim_t breathing_anim;
  int state;            /* enum ui_state_e */
  int trigger_blinks;   /* remaining blink count */
} audio_event_ui_t;

/* ── Public API ──────────────────────────────────────────────── */

/**
 * Initialize LVGL backend and create the dashboard UI.
 * @return 0 on success, negative errno on failure.
 */
int audio_event_ui_init(void);

/**
 * Destroy the UI and deinitialize LVGL.
 */
void audio_event_ui_deinit(void);

/**
 * Update the waveform display with new PCM samples.
 */
void audio_event_ui_update_waveform(const int16_t *samples,
                                    uint32_t count);

/**
 * Update the probability bar chart.
 * @param probs       Array of probabilities [0.0, 1.0].
 * @param num_classes Number of classes (should be AUDIO_EVENT_CLASS_COUNT).
 */
void audio_event_ui_update_probs(const float *probs, int num_classes);

/**
 * Notify the UI that a detection event has fired.
 */
void audio_event_ui_notify_detection(int class_id, int confidence);

/**
 * Notify the UI to enter cooldown display.
 * @param duration_ms Total cooldown duration in milliseconds.
 */
void audio_event_ui_notify_cooldown(uint32_t duration_ms);

/**
 * Update the cooldown progress bar.
 * @param remaining_ms Remaining cooldown time in milliseconds.
 */
void audio_event_ui_update_cooldown(uint32_t remaining_ms);

/**
 * Reset the UI to the listening state.
 */
void audio_event_ui_set_listening(void);

/**
 * Check if the UI is initialized.
 * @return true if UI is active.
 */
bool audio_event_ui_is_active(void);

/**
 * Periodic tick to update clock and housekeeping.
 * Call from the main loop with elapsed milliseconds since last call.
 */
void audio_event_ui_tick(uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif
