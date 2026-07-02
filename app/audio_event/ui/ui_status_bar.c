/**
 * apps/examples/audio_event/ui/ui_status_bar.c
 *
 * Minimal top status bar: just clock on the right, LED dot on the left.
 * Title is handled separately in audio_event_ui.c.
 */

#include "audio_event_ui.h"

#include <stdio.h>

void ui_status_bar_create(audio_event_ui_t *ui, lv_obj_t *parent)
{
  lv_obj_t *bar;

  /* Container — blends into background, no card styling */
  bar = lv_obj_create(parent);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, UI_SCREEN_W, UI_STATUS_BAR_H);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, UI_STATUS_BAR_Y);
  lv_obj_set_style_pad_hor(bar, UI_PAD_X, 0);
  ui->status_bar = bar;

  /* LED indicator (small dot, left side) */
  ui->status_led = lv_obj_create(bar);
  lv_obj_remove_style_all(ui->status_led);
  lv_obj_set_size(ui->status_led, 8, 8);
  lv_obj_set_style_bg_color(ui->status_led, UI_COLOR_LED_GREEN, 0);
  lv_obj_set_style_bg_opa(ui->status_led, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ui->status_led, LV_RADIUS_CIRCLE, 0);
  lv_obj_align(ui->status_led, LV_ALIGN_LEFT_MID, 0, 0);

  /* Clock label (right side) */
  ui->status_clock = lv_label_create(bar);
  lv_label_set_text(ui->status_clock, "00:00");
  lv_obj_set_style_text_font(ui->status_clock, UI_FONT_TINY, 0);
  lv_obj_set_style_text_color(ui->status_clock, UI_COLOR_TEXT_DIM, 0);
  lv_obj_align(ui->status_clock, LV_ALIGN_RIGHT_MID, 0, 0);

  /* State label (center, small) */
  ui->status_state = lv_label_create(bar);
  lv_label_set_text(ui->status_state, "");
  lv_obj_set_style_text_font(ui->status_state, UI_FONT_TINY, 0);
  lv_obj_set_style_text_color(ui->status_state, UI_COLOR_LED_GREEN, 0);
  lv_obj_center(ui->status_state);
}

void ui_status_bar_set_state(audio_event_ui_t *ui, int state)
{
  if (!ui || !ui->status_state || !ui->status_led)
    {
      return;
    }

  switch (state)
    {
      case UI_STATE_LISTENING:
        lv_label_set_text(ui->status_state, "");
        lv_obj_set_style_bg_color(ui->status_led,
                                  UI_COLOR_LED_GREEN, 0);
        break;

      case UI_STATE_TRIGGERED:
        lv_label_set_text(ui->status_state, "ACTIVE");
        lv_obj_set_style_text_color(ui->status_state,
                                    UI_COLOR_LED_RED, 0);
        lv_obj_set_style_bg_color(ui->status_led,
                                  UI_COLOR_LED_RED, 0);
        break;

      case UI_STATE_COOLDOWN:
        lv_label_set_text(ui->status_state, "COOLDOWN");
        lv_obj_set_style_text_color(ui->status_state,
                                    UI_COLOR_LED_YELLOW, 0);
        lv_obj_set_style_bg_color(ui->status_led,
                                  UI_COLOR_LED_YELLOW, 0);
        break;

      default:
        break;
    }
}

void ui_status_bar_update_clock(audio_event_ui_t *ui, uint32_t ms)
{
  char buf[16];
  unsigned int sec;
  unsigned int min;

  if (!ui || !ui->status_clock)
    {
      return;
    }

  sec = (unsigned int)(ms / 1000);
  min = sec / 60;
  sec = sec % 60;
  if (min > 99)
    {
      min = 99;
    }

  snprintf(buf, sizeof(buf), "%02u:%02u", min, sec);
  lv_label_set_text(ui->status_clock, buf);
}
