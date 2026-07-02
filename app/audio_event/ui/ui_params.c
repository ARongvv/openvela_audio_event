/**
 * apps/examples/audio_event/ui/ui_params.c
 *
 * Bottom parameter bar — full-width capsule card with rounded corners.
 */

#include "audio_event_ui.h"

#include <nuttx/config.h>
#include <stdio.h>

void ui_params_create(audio_event_ui_t *ui, lv_obj_t *parent)
{
  char buf[32];

  /* Capsule card at the bottom */
  ui->params_bar = lv_obj_create(parent);
  lv_obj_remove_style_all(ui->params_bar);
  lv_obj_set_size(ui->params_bar,
                  UI_SCREEN_W - 2 * UI_PAD_X, UI_PARAMS_H);
  lv_obj_align(ui->params_bar, LV_ALIGN_TOP_LEFT,
               UI_PAD_X, UI_PARAMS_Y);
  lv_obj_set_style_bg_color(ui->params_bar, UI_COLOR_CARD_BG, 0);
  lv_obj_set_style_bg_opa(ui->params_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ui->params_bar, UI_CARD_RADIUS, 0);
  lv_obj_set_style_border_color(ui->params_bar, UI_COLOR_BORDER, 0);
  lv_obj_set_style_border_width(ui->params_bar, UI_CARD_BORDER_W, 0);
  lv_obj_set_flex_flow(ui->params_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ui->params_bar, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  /* Threshold */
  snprintf(buf, sizeof(buf), "Thr %d%%",
           CONFIG_EXAMPLES_AUDIO_EVENT_KNOCK_THRESHOLD / 10);
  ui->param_threshold = lv_label_create(ui->params_bar);
  lv_label_set_text(ui->param_threshold, buf);
  lv_obj_set_style_text_font(ui->param_threshold, UI_FONT_TINY, 0);
  lv_obj_set_style_text_color(ui->param_threshold,
                              UI_COLOR_TEXT, 0);

  /* Consecutive hits */
  snprintf(buf, sizeof(buf), "Hit %d",
           CONFIG_EXAMPLES_AUDIO_EVENT_CONSECUTIVE_HITS);
  ui->param_consecutive = lv_label_create(ui->params_bar);
  lv_label_set_text(ui->param_consecutive, buf);
  lv_obj_set_style_text_font(ui->param_consecutive, UI_FONT_TINY, 0);
  lv_obj_set_style_text_color(ui->param_consecutive,
                              UI_COLOR_TEXT, 0);

  /* Cooldown */
  snprintf(buf, sizeof(buf), "CD %.1fs",
           CONFIG_EXAMPLES_AUDIO_EVENT_COOLDOWN_MS / 1000.0f);
  ui->param_cooldown = lv_label_create(ui->params_bar);
  lv_label_set_text(ui->param_cooldown, buf);
  lv_obj_set_style_text_font(ui->param_cooldown, UI_FONT_TINY, 0);
  lv_obj_set_style_text_color(ui->param_cooldown,
                              UI_COLOR_TEXT, 0);
}
