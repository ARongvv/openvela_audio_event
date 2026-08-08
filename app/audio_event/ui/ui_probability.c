/**
 * apps/examples/audio_event/ui/ui_probability.c
 *
 * Probability bar chart (Knock / Cough / Glass / Yes / No / Stop / Bgnd / Quiet).
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

void ui_probability_create(audio_event_ui_t *ui, lv_obj_t *parent)
{
  lv_color_t class_colors[AUDIO_EVENT_CLASS_COUNT];
  int i;

  /* Initialize colors at runtime (lv_color_hex is not a constant) */
  class_colors[0] = UI_COLOR_KNOCK;
  class_colors[1] = UI_COLOR_COUGH;
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_MODEL_8CLASS
  class_colors[2] = UI_COLOR_GLASS;
  class_colors[3] = UI_COLOR_YES;
  class_colors[4] = UI_COLOR_NO;
  class_colors[5] = UI_COLOR_STOP;
  class_colors[6] = UI_COLOR_BACKGROUND;
  class_colors[7] = UI_COLOR_SILENCE;
#else
  class_colors[2] = UI_COLOR_BACKGROUND;
  class_colors[3] = UI_COLOR_SILENCE;
#endif

  /* Outer container — fills the probability card */
  ui->prob_container = lv_obj_create(parent);
  lv_obj_remove_style_all(ui->prob_container);
  lv_obj_set_size(ui->prob_container,
                  LV_PCT(100), LV_PCT(100));
  lv_obj_align(ui->prob_container, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_flex_flow(ui->prob_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(ui->prob_container, 0, 0);
  lv_obj_set_style_pad_row(ui->prob_container, 2, 0);

  for (i = 0; i < AUDIO_EVENT_CLASS_COUNT; i++)
    {
      lv_obj_t *row;
      char val_buf[8];

      /* Row container */
      row = lv_obj_create(ui->prob_container);
      lv_obj_remove_style_all(row);
      lv_obj_set_size(row, LV_PCT(100), UI_PROB_ROW_H);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(row, 4, 0);

      /* Class name label */
      ui->prob_labels[i] = lv_label_create(row);
      lv_label_set_text(ui->prob_labels[i], g_class_names[i]);
      lv_label_set_long_mode(ui->prob_labels[i], LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_font(ui->prob_labels[i], UI_FONT_PROB, 0);
      lv_obj_set_style_text_color(ui->prob_labels[i],
                                  class_colors[i], 0);
      lv_obj_set_width(ui->prob_labels[i], UI_PROB_LABEL_W);

      /* Probability bar */
      ui->prob_bars[i] = lv_bar_create(row);
      lv_obj_set_size(ui->prob_bars[i], UI_PROB_BAR_W, 10);
      lv_bar_set_range(ui->prob_bars[i], 0, 100);
      lv_bar_set_value(ui->prob_bars[i], 0, LV_ANIM_OFF);
      lv_obj_set_style_bg_color(ui->prob_bars[i],
                                UI_COLOR_BAR_BG, 0);
      lv_obj_set_style_bg_opa(ui->prob_bars[i],
                              LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(ui->prob_bars[i],
                                UI_COLOR_BAR_INACTIVE,
                                LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(ui->prob_bars[i],
                              LV_OPA_COVER,
                              LV_PART_INDICATOR);
      lv_obj_set_style_radius(ui->prob_bars[i], 3, 0);
      lv_obj_set_style_radius(ui->prob_bars[i], 3,
                              LV_PART_INDICATOR);

      /* Value label */
      snprintf(val_buf, sizeof(val_buf), "  0%%");
      ui->prob_values[i] = lv_label_create(row);
      lv_label_set_text(ui->prob_values[i], val_buf);
      lv_label_set_long_mode(ui->prob_values[i], LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_font(ui->prob_values[i], UI_FONT_PROB, 0);
      lv_obj_set_style_text_color(ui->prob_values[i],
                                  UI_COLOR_TEXT, 0);
      lv_obj_set_width(ui->prob_values[i], UI_PROB_VALUE_W);
    }
}

void ui_probability_update(audio_event_ui_t *ui,
                           const float *probs, int num_classes)
{
  int best = 0;
  int i;

  if (!ui || !probs || num_classes <= 0)
    {
      return;
    }

  /* Find best class */
  for (i = 1; i < num_classes; i++)
    {
      if (probs[i] > probs[best])
        {
          best = i;
        }
    }

  for (i = 0; i < num_classes && i < AUDIO_EVENT_CLASS_COUNT; i++)
    {
      int pct = (int)(probs[i] * 100.0f + 0.5f);
      char val_buf[8];

      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;

      /* Update bar value with animation */
      lv_bar_set_value(ui->prob_bars[i], pct, LV_ANIM_ON);

      /* Highlight best class */
      if (i == best)
        {
          lv_obj_set_style_bg_color(ui->prob_bars[i],
                                    UI_COLOR_BAR_ACTIVE,
                                    LV_PART_INDICATOR);
        }
      else
        {
          lv_obj_set_style_bg_color(ui->prob_bars[i],
                                    UI_COLOR_BAR_INACTIVE,
                                    LV_PART_INDICATOR);
        }

      /* Update value text */
      snprintf(val_buf, sizeof(val_buf), "%3d%%", pct);
      lv_label_set_text(ui->prob_values[i], val_buf);
    }
}
