/**
 * apps/examples/audio_event/ui/ui_waveform.c
 *
 * Real-time audio waveform visualization using lv_canvas.
 * Positioned inside the core card (top-left area).
 */

#include "audio_event_ui.h"

#include <string.h>

void ui_waveform_create(audio_event_ui_t *ui, lv_obj_t *parent)
{
  lv_obj_t *container;
  size_t buf_size;

  /* Container inside waveform card, top-left */
  container = lv_obj_create(parent);
  lv_obj_remove_style_all(container);
  lv_obj_set_size(container, UI_WAVEFORM_W + 4, UI_WAVEFORM_H + 4);
  lv_obj_align(container, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(container, UI_COLOR_WAVEFORM_BG, 0);
  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(container, 4, 0);
  lv_obj_set_style_border_color(container, UI_COLOR_BORDER, 0);
  lv_obj_set_style_border_width(container, 1, 0);
  lv_obj_set_style_pad_all(container, 2, 0);
  ui->waveform_container = container;

  /* Allocate canvas buffer */
  buf_size = UI_WAVEFORM_W * UI_WAVEFORM_H * sizeof(lv_color_t);
  ui->canvas_buf = lv_malloc(buf_size);
  if (!ui->canvas_buf)
    {
      return;
    }

  memset(ui->canvas_buf, 0, buf_size);

  /* Canvas */
  ui->waveform_canvas = lv_canvas_create(container);
  lv_canvas_set_buffer(ui->waveform_canvas,
                       ui->canvas_buf,
                       UI_WAVEFORM_W,
                       UI_WAVEFORM_H,
                       LV_COLOR_FORMAT_NATIVE);
  lv_obj_center(ui->waveform_canvas);

  /* Fill background */
  lv_canvas_fill_bg(ui->waveform_canvas,
                    UI_COLOR_WAVEFORM_BG,
                    LV_OPA_COVER);
}

void ui_waveform_draw(audio_event_ui_t *ui,
                      const int16_t *samples, uint32_t count)
{
  lv_layer_t layer;
  lv_draw_line_dsc_t line_dsc;
  uint32_t i;
  int mid_y;
  int step;
  uint32_t num_lines;

  if (!ui || !ui->waveform_canvas || !samples || count == 0)
    {
      return;
    }

  /* Clear canvas */
  lv_canvas_fill_bg(ui->waveform_canvas,
                    UI_COLOR_WAVEFORM_BG,
                    LV_OPA_COVER);

  /* Initialize draw layer */
  lv_canvas_init_layer(ui->waveform_canvas, &layer);

  /* Prepare line style */
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = UI_COLOR_WAVEFORM;
  line_dsc.width = 1;
  line_dsc.opa = LV_OPA_COVER;

  mid_y = UI_WAVEFORM_H / 2;

  /* Downsample: draw at most UI_WAVEFORM_W line segments */
  num_lines = count;
  if (num_lines > (uint32_t)UI_WAVEFORM_W)
    {
      num_lines = (uint32_t)UI_WAVEFORM_W;
    }

  step = (int)(count / num_lines);
  if (step < 1)
    {
      step = 1;
    }

  for (i = 0; i < num_lines - 1; i++)
    {
      int x0 = (int)(i * UI_WAVEFORM_W / num_lines);
      int x1 = (int)((i + 1) * UI_WAVEFORM_W / num_lines);
      int idx0 = (int)(i * step);
      int idx1 = (int)((i + 1) * step);
      int y0;
      int y1;

      /* Map int16 [-32768, 32767] to [UI_WAVEFORM_H-1, 0] */
      y0 = mid_y - (int)((int32_t)samples[idx0] * mid_y / 32768);
      y1 = mid_y - (int)((int32_t)samples[idx1] * mid_y / 32768);

      /* Clamp */
      if (y0 < 0) y0 = 0;
      if (y0 >= UI_WAVEFORM_H) y0 = UI_WAVEFORM_H - 1;
      if (y1 < 0) y1 = 0;
      if (y1 >= UI_WAVEFORM_H) y1 = UI_WAVEFORM_H - 1;

      /* Set endpoints in the draw descriptor */
      line_dsc.p1.x = x0;
      line_dsc.p1.y = y0;
      line_dsc.p2.x = x1;
      line_dsc.p2.y = y1;

      lv_draw_line(&layer, &line_dsc);
    }

  lv_canvas_finish_layer(ui->waveform_canvas, &layer);
}
