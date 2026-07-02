/**
 * apps/examples/audio_event/ui/audio_event_ui.c
 *
 * Main UI controller: LVGL init, screen assembly, public API dispatch.
 */

#include "audio_event_ui.h"

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/boardctl.h>

#include "ui_theme.h"

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
#include <uv.h>
#endif

/* ── Internal sub-module prototypes ──────────────────────────── */

/* Status bar (ui_status_bar.c) */
extern void ui_status_bar_create(audio_event_ui_t *ui,
                                 lv_obj_t *parent);
extern void ui_status_bar_set_state(audio_event_ui_t *ui, int state);
extern void ui_status_bar_update_clock(audio_event_ui_t *ui,
                                       uint32_t ms);

/* Waveform (ui_waveform.c) */
extern void ui_waveform_create(audio_event_ui_t *ui,
                               lv_obj_t *parent);
extern void ui_waveform_draw(audio_event_ui_t *ui,
                             const int16_t *samples, uint32_t count);

/* Probability (ui_probability.c) */
extern void ui_probability_create(audio_event_ui_t *ui,
                                  lv_obj_t *parent);
extern void ui_probability_update(audio_event_ui_t *ui,
                                  const float *probs, int num_classes);

/* Detection (ui_detection.c) */
extern void ui_detection_create(audio_event_ui_t *ui,
                                lv_obj_t *parent);
extern void ui_detection_show_event(audio_event_ui_t *ui,
                                    int class_id, int confidence);
extern void ui_detection_show_cooldown(audio_event_ui_t *ui,
                                       uint32_t duration_ms);
extern void ui_detection_update_cooldown(audio_event_ui_t *ui,
                                         uint32_t remaining_ms,
                                         uint32_t total_ms);
extern void ui_detection_set_listening(audio_event_ui_t *ui);

/* Params (ui_params.c) */
extern void ui_params_create(audio_event_ui_t *ui, lv_obj_t *parent);

/* Animations (ui_anim.c) */
extern void ui_anim_start_breathing(audio_event_ui_t *ui);
extern void ui_anim_stop_breathing(audio_event_ui_t *ui);
extern void ui_anim_blink_detection(audio_event_ui_t *ui);

/* ── Global UI state ─────────────────────────────────────────── */

static audio_event_ui_t g_ui;
static bool g_ui_active = false;
static uint32_t g_cooldown_total_ms = 0;
static uint32_t g_elapsed_ms = 0;

/* ── LVGL NuttX integration ──────────────────────────────────── */

#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#define UI_NEED_BOARDINIT 1
#endif

/* ── Public API implementation ───────────────────────────────── */

int audio_event_ui_init(void)
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;

  memset(&g_ui, 0, sizeof(g_ui));
  g_ui.state = UI_STATE_LISTENING;
  g_cooldown_total_ms = 0;
  g_elapsed_ms = 0;

#ifdef UI_NEED_BOARDINIT
  boardctl(BOARDIOC_INIT, 0);
#endif

  /* Initialize LVGL core */
  lv_init();

  /* Initialize NuttX display backend */
  lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif

#ifdef CONFIG_INPUT_TOUCHSCREEN
  info.input_path = "/dev/input0";
#endif

  lv_nuttx_init(&info, &result);
  if (!result.disp)
    {
      printf("[audio_event_ui] display init failed\n");
      lv_deinit();
      return -ENODEV;
    }

  printf("[audio_event_ui] disp=%p\n", (void *)result.disp);

  /* Create main screen */
  g_ui.screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(g_ui.screen, UI_COLOR_BG, 0);
  lv_obj_set_style_bg_opa(g_ui.screen, LV_OPA_COVER, 0);

  /* Status bar (top, no card, blends into background) */
  ui_status_bar_create(&g_ui, g_ui.screen);

  /* Title area */
  {
    lv_obj_t *title = lv_label_create(g_ui.screen);
    lv_label_set_text(title, LV_SYMBOL_AUDIO " AudioEvent");
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, UI_PAD_X, UI_TITLE_Y);
  }

  /* Waveform card (left) */
  g_ui.waveform_card = lv_obj_create(g_ui.screen);
  lv_obj_remove_style_all(g_ui.waveform_card);
  lv_obj_set_size(g_ui.waveform_card, 152, UI_WAVECARD_H);
  lv_obj_align(g_ui.waveform_card, LV_ALIGN_TOP_LEFT,
               UI_PAD_X, UI_WAVECARD_Y);
  lv_obj_set_style_bg_color(g_ui.waveform_card, UI_COLOR_CARD_BG, 0);
  lv_obj_set_style_bg_opa(g_ui.waveform_card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_ui.waveform_card, UI_CARD_RADIUS, 0);
  lv_obj_set_style_border_color(g_ui.waveform_card,
                                UI_COLOR_BORDER, 0);
  lv_obj_set_style_border_width(g_ui.waveform_card,
                                UI_CARD_BORDER_W, 0);
  lv_obj_set_style_pad_all(g_ui.waveform_card, UI_PAD_X, 0);

  /* Probability card (right) */
  g_ui.prob_card = lv_obj_create(g_ui.screen);
  lv_obj_remove_style_all(g_ui.prob_card);
  lv_obj_set_size(g_ui.prob_card, 156, UI_PROBCARD_H);
  lv_obj_align(g_ui.prob_card, LV_ALIGN_TOP_LEFT,
               UI_PAD_X + 152 + UI_GAP, UI_PROBCARD_Y);
  lv_obj_set_style_bg_color(g_ui.prob_card, UI_COLOR_CARD_BG, 0);
  lv_obj_set_style_bg_opa(g_ui.prob_card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_ui.prob_card, UI_CARD_RADIUS, 0);
  lv_obj_set_style_border_color(g_ui.prob_card,
                                UI_COLOR_BORDER, 0);
  lv_obj_set_style_border_width(g_ui.prob_card,
                                UI_CARD_BORDER_W, 0);
  lv_obj_set_style_pad_all(g_ui.prob_card, 4, 0);

  /* Detection card (below, full width) */
  g_ui.detection_card = lv_obj_create(g_ui.screen);
  lv_obj_remove_style_all(g_ui.detection_card);
  lv_obj_set_size(g_ui.detection_card,
                  UI_SCREEN_W - 2 * UI_PAD_X, UI_DETECTCARD_H);
  lv_obj_align(g_ui.detection_card, LV_ALIGN_TOP_LEFT,
               UI_PAD_X, UI_DETECTCARD_Y);
  lv_obj_set_style_bg_color(g_ui.detection_card, UI_COLOR_CARD_BG, 0);
  lv_obj_set_style_bg_opa(g_ui.detection_card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_ui.detection_card, UI_CARD_RADIUS, 0);
  lv_obj_set_style_border_color(g_ui.detection_card,
                                UI_COLOR_BORDER, 0);
  lv_obj_set_style_border_width(g_ui.detection_card,
                                UI_CARD_BORDER_W, 0);
  lv_obj_set_style_pad_all(g_ui.detection_card, 4, 0);

  /* Build components inside their respective cards */
  ui_waveform_create(&g_ui, g_ui.waveform_card);
  ui_probability_create(&g_ui, g_ui.prob_card);
  ui_detection_create(&g_ui, g_ui.detection_card);

  /* Bottom params capsule */
  ui_params_create(&g_ui, g_ui.screen);

  /* Load screen */
  lv_scr_load(g_ui.screen);

  g_ui_active = true;
  printf("[audio_event_ui] init OK\n");
  return 0;
}

void audio_event_ui_deinit(void)
{
  if (!g_ui_active)
    {
      return;
    }

  /* Stop animations */
  ui_anim_stop_breathing(&g_ui);

  /* Free canvas buffer */
  if (g_ui.canvas_buf)
    {
      lv_free(g_ui.canvas_buf);
      g_ui.canvas_buf = NULL;
    }

  /* Delete screen */
  if (g_ui.screen)
    {
      lv_obj_del(g_ui.screen);
      g_ui.screen = NULL;
    }

  g_ui_active = false;
  printf("[audio_event_ui] deinit\n");
}

void audio_event_ui_update_waveform(const int16_t *samples,
                                    uint32_t count)
{
  if (!g_ui_active)
    {
      return;
    }

  ui_waveform_draw(&g_ui, samples, count);
}

void audio_event_ui_update_probs(const float *probs, int num_classes)
{
  if (!g_ui_active)
    {
      return;
    }

  ui_probability_update(&g_ui, probs, num_classes);
}

void audio_event_ui_notify_detection(int class_id, int confidence)
{
  if (!g_ui_active)
    {
      return;
    }

  g_ui.state = UI_STATE_TRIGGERED;

  /* Update status bar */
  ui_status_bar_set_state(&g_ui, UI_STATE_TRIGGERED);
  ui_anim_start_breathing(&g_ui);

  /* Show detection event */
  ui_detection_show_event(&g_ui, class_id, confidence);
  ui_anim_blink_detection(&g_ui);
}

void audio_event_ui_notify_cooldown(uint32_t duration_ms)
{
  if (!g_ui_active)
    {
      return;
    }

  g_ui.state = UI_STATE_COOLDOWN;
  g_cooldown_total_ms = duration_ms;

  /* Update status bar */
  ui_anim_stop_breathing(&g_ui);
  ui_status_bar_set_state(&g_ui, UI_STATE_COOLDOWN);

  /* Show cooldown UI */
  ui_detection_show_cooldown(&g_ui, duration_ms);
}

void audio_event_ui_update_cooldown(uint32_t remaining_ms)
{
  if (!g_ui_active)
    {
      return;
    }

  ui_detection_update_cooldown(&g_ui, remaining_ms,
                               g_cooldown_total_ms);
}

void audio_event_ui_set_listening(void)
{
  if (!g_ui_active)
    {
      return;
    }

  g_ui.state = UI_STATE_LISTENING;
  g_cooldown_total_ms = 0;

  /* Update status bar */
  ui_anim_stop_breathing(&g_ui);
  ui_status_bar_set_state(&g_ui, UI_STATE_LISTENING);

  /* Reset detection area */
  ui_detection_set_listening(&g_ui);
}

bool audio_event_ui_is_active(void)
{
  return g_ui_active;
}

void audio_event_ui_tick(uint32_t elapsed_ms)
{
  if (!g_ui_active)
    {
      return;
    }

  g_elapsed_ms += elapsed_ms;

  /* Update clock every second */
  ui_status_bar_update_clock(&g_ui, g_elapsed_ms);
}
