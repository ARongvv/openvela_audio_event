/**
 * apps/examples/audio_event/ui/ui_detection.c
 *
 * Detection status area + cooldown progress bar.
 */

#include "audio_event_ui.h"

#include <stdio.h>

static const char *const g_class_names[AUDIO_EVENT_CLASS_COUNT] =
{
  UI_CLASS_NAME_KNOCK,
  UI_CLASS_NAME_COUGH,
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_MODEL_8CLASS
  UI_CLASS_NAME_GLASS,
  UI_CLASS_NAME_YES,
  UI_CLASS_NAME_NO,
  UI_CLASS_NAME_STOP,
#endif
  UI_CLASS_NAME_BACKGROUND,
  UI_CLASS_NAME_SILENCE
};

void ui_detection_create(audio_event_ui_t *ui, lv_obj_t *parent)
{
  /* Detection status container — centered inside detection card */
  ui->detection_container = lv_obj_create(parent);
  lv_obj_remove_style_all(ui->detection_container);
  lv_obj_set_size(ui->detection_container,
                  LV_PCT(100), LV_PCT(100));
  lv_obj_align(ui->detection_container, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(ui->detection_container,
                            UI_COLOR_CARD_BG, 0);
  lv_obj_set_style_bg_opa(ui->detection_container,
                          LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(ui->detection_container, 6, 0);
  lv_obj_set_style_border_width(ui->detection_container, 0, 0);
  lv_obj_set_flex_flow(ui->detection_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ui->detection_container,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ui->detection_container, 8, 0);

  /* Main detection label */
  ui->detection_label = lv_label_create(ui->detection_container);
  lv_label_set_text(ui->detection_label, LV_SYMBOL_AUDIO
                    " Listening...");
  lv_obj_set_style_text_font(ui->detection_label, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_color(ui->detection_label,
                              UI_COLOR_LED_GREEN, 0);

  /* Confidence label (hidden initially, shown inline next to event name) */
  ui->detection_confidence = lv_label_create(ui->detection_container);
  lv_label_set_text(ui->detection_confidence, "");
  lv_obj_set_style_text_font(ui->detection_confidence,
                             UI_FONT_TINY, 0);
  lv_obj_set_style_text_color(ui->detection_confidence,
                              UI_COLOR_TEXT, 0);
  lv_obj_add_flag(ui->detection_confidence, LV_OBJ_FLAG_HIDDEN);

  /* Cooldown bar — inside detection card */
  ui->cooldown_bar = lv_bar_create(parent);
  lv_obj_set_size(ui->cooldown_bar,
                  LV_PCT(90), UI_COOLDOWN_H);
  lv_obj_align(ui->cooldown_bar, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_bar_set_range(ui->cooldown_bar, 0, 1000);
  lv_bar_set_value(ui->cooldown_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(ui->cooldown_bar,
                            UI_COLOR_BAR_BG, 0);
  lv_obj_set_style_bg_opa(ui->cooldown_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(ui->cooldown_bar,
                            UI_COLOR_LED_YELLOW,
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(ui->cooldown_bar,
                          LV_OPA_COVER,
                          LV_PART_INDICATOR);
  lv_obj_set_style_radius(ui->cooldown_bar, 3, 0);
  lv_obj_set_style_radius(ui->cooldown_bar, 3,
                          LV_PART_INDICATOR);
  lv_obj_add_flag(ui->cooldown_bar, LV_OBJ_FLAG_HIDDEN);

  /* Cooldown label — next to cooldown bar */
  ui->cooldown_label = lv_label_create(parent);
  lv_label_set_text(ui->cooldown_label, "");
  lv_obj_align(ui->cooldown_label, LV_ALIGN_BOTTOM_MID,
               0, -(UI_COOLDOWN_H + 4));
  lv_obj_set_style_text_font(ui->cooldown_label, UI_FONT_TINY, 0);
  lv_obj_set_style_text_color(ui->cooldown_label,
                              UI_COLOR_LED_YELLOW, 0);
  lv_obj_add_flag(ui->cooldown_label, LV_OBJ_FLAG_HIDDEN);
}

void ui_detection_show_event(audio_event_ui_t *ui,
                             int class_id, int confidence)
{
  char buf[48];

  if (!ui || !ui->detection_label)
    {
      return;
    }

  if (class_id < 0 || class_id >= AUDIO_EVENT_CLASS_COUNT)
    {
      return;
    }

  snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s Detected!",
           g_class_names[class_id]);
  lv_label_set_text(ui->detection_label, buf);
  lv_obj_set_style_text_color(ui->detection_label,
                              UI_COLOR_LED_RED, 0);

  /* Show confidence inline */
  snprintf(buf, sizeof(buf), "%d/1000", confidence);
  lv_label_set_text(ui->detection_confidence, buf);
  lv_obj_clear_flag(ui->detection_confidence, LV_OBJ_FLAG_HIDDEN);
}

void ui_detection_show_cooldown(audio_event_ui_t *ui,
                                uint32_t duration_ms)
{
  if (!ui)
    {
      return;
    }

  /* Show cooldown bar */
  if (ui->cooldown_bar)
    {
      lv_obj_clear_flag(ui->cooldown_bar, LV_OBJ_FLAG_HIDDEN);
      lv_bar_set_value(ui->cooldown_bar, 1000, LV_ANIM_OFF);
    }

  if (ui->cooldown_label)
    {
      char buf[32];
      snprintf(buf, sizeof(buf), "Cooldown %.1fs",
               duration_ms / 1000.0f);
      lv_label_set_text(ui->cooldown_label, buf);
      lv_obj_clear_flag(ui->cooldown_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_detection_update_cooldown(audio_event_ui_t *ui,
                                  uint32_t remaining_ms,
                                  uint32_t total_ms)
{
  int value;

  if (!ui || !ui->cooldown_bar || total_ms == 0)
    {
      return;
    }

  value = (int)((uint64_t)remaining_ms * 1000 / total_ms);
  if (value < 0) value = 0;
  if (value > 1000) value = 1000;

  lv_bar_set_value(ui->cooldown_bar, value, LV_ANIM_OFF);

  if (ui->cooldown_label)
    {
      char buf[32];
      snprintf(buf, sizeof(buf), "Cooldown %.1fs",
               remaining_ms / 1000.0f);
      lv_label_set_text(ui->cooldown_label, buf);
    }
}

void ui_detection_set_listening(audio_event_ui_t *ui)
{
  if (!ui)
    {
      return;
    }

  if (ui->detection_label)
    {
      lv_label_set_text(ui->detection_label,
                        LV_SYMBOL_AUDIO " Listening...");
      lv_obj_set_style_text_color(ui->detection_label,
                                  UI_COLOR_LED_GREEN, 0);
    }

  if (ui->detection_confidence)
    {
      lv_obj_add_flag(ui->detection_confidence, LV_OBJ_FLAG_HIDDEN);
    }

  if (ui->cooldown_bar)
    {
      lv_obj_add_flag(ui->cooldown_bar, LV_OBJ_FLAG_HIDDEN);
    }

  if (ui->cooldown_label)
    {
      lv_obj_add_flag(ui->cooldown_label, LV_OBJ_FLAG_HIDDEN);
    }
}
