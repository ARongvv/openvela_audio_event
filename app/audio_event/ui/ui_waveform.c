/**
 * apps/examples/audio_event/ui/ui_waveform.c
 *
 * Real-time scrolling audio waveform visualization using lv_canvas.
 *
 * Maintains a 2-second ring buffer (32000 samples @16 kHz) and draws a
 * scrolling symmetric-bar waveform.  Each pixel column finds the min and
 * max sample across ~242 samples, then draws a vertical bar above and
 * below the centre line.  A display-only gain (×6) makes quiet audio
 * visible without touching the original PCM data.
 *
 * Positioned inside the waveform card (top-left area).
 */

#include "audio_event_ui.h"

#include <string.h>

/* ── Tunable constants ─────────────────────────────────────────── */

#define UI_WAVEFORM_WINDOW_SAMPLES  32000  /* 2 seconds @ 16 kHz             */
#define UI_WAVEFORM_GAIN            6      /* display-only amplitude boost   */

/* ── Ring buffer ────────────────────────────────────────────────── */

static int16_t *g_waveform_ring;
static uint32_t g_waveform_wr;            /* write cursor (monotonic)        */
static uint32_t g_waveform_total;         /* total samples ever appended     */

/* ── Helpers ────────────────────────────────────────────────────── */

static inline int16_t ring_get(uint32_t idx)
{
  return g_waveform_ring[idx % UI_WAVEFORM_WINDOW_SAMPLES];
}

/* ── Public API ─────────────────────────────────────────────────── */

void ui_waveform_create(audio_event_ui_t *ui, lv_obj_t *parent)
{
  lv_obj_t *container;
  size_t    ring_size;
  size_t    buf_size;

  /* Reset ring state */
  g_waveform_wr    = 0;
  g_waveform_total = 0;
  ring_size = UI_WAVEFORM_WINDOW_SAMPLES * sizeof(g_waveform_ring[0]);
  if (g_waveform_ring == NULL)
    {
      g_waveform_ring = lv_malloc(ring_size);
    }

  if (g_waveform_ring != NULL)
    {
      memset(g_waveform_ring, 0, ring_size);
    }

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

void ui_waveform_destroy(void)
{
  if (g_waveform_ring != NULL)
    {
      lv_free(g_waveform_ring);
      g_waveform_ring = NULL;
    }

  g_waveform_wr    = 0;
  g_waveform_total = 0;
}

void ui_waveform_draw(audio_event_ui_t *ui,
                      const int16_t *samples, uint32_t count)
{
  lv_layer_t         layer;
  lv_draw_line_dsc_t line_dsc;
  uint32_t           i;
  int                mid_y;
  int                half_h;      /* half canvas height in pixels            */
  uint32_t           available;
  uint32_t           start;       /* logical index of oldest visible sample  */

  if (!ui || !ui->waveform_canvas || !g_waveform_ring ||
      !samples || count == 0)
    {
      return;
    }

  /* ── 1. Append new samples to ring ────────────────────────────── */

  for (i = 0; i < count; i++)
    {
      g_waveform_ring[g_waveform_wr % UI_WAVEFORM_WINDOW_SAMPLES] =
          samples[i];
      g_waveform_wr++;
    }

  g_waveform_total += count;

  /* ── 2. Clear canvas ──────────────────────────────────────────── */

  lv_canvas_fill_bg(ui->waveform_canvas,
                    UI_COLOR_WAVEFORM_BG,
                    LV_OPA_COVER);

  /* ── 3. Init draw layer ───────────────────────────────────────── */

  lv_canvas_init_layer(ui->waveform_canvas, &layer);

  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = UI_COLOR_WAVEFORM;
  line_dsc.width = 1;
  line_dsc.opa   = LV_OPA_COVER;

  mid_y  = UI_WAVEFORM_H / 2;
  half_h = mid_y;                          /* 42 for 84-px canvas */

  /* ── 4. Visible-range bounds ──────────────────────────────────── */

  available = g_waveform_total;
  if (available > (uint32_t)UI_WAVEFORM_WINDOW_SAMPLES)
    {
      available = (uint32_t)UI_WAVEFORM_WINDOW_SAMPLES;
    }

  /* Logical start of visible window.
   * During the initial fill period (total < 2 s), `available` is
   * smaller; each pixel column spans fewer samples and the waveform
   * naturally "grows in" from the left. */
  start = g_waveform_wr - available;

  /* ── 5. Per-column symmetric bar ─────────────────────────────────
   *
   * For each of the 132 pixel columns, scan the ~242 samples that fall
   * into that column.  Find the true min (most negative) and max (most
   * positive), apply gain, then draw a vertical line from the upper
   * bound to the lower bound, mirrored around the centre.  A column
   * with zero amplitude still gets a 1-px dot so silent audio shows a
   * thin centreline rather than disappearing entirely. */

  for (i = 0; i < (uint32_t)UI_WAVEFORM_W; i++)
    {
      uint32_t col_start;
      uint32_t col_end;
      uint32_t j;
      int32_t  col_min = 0;               /* most negative sample in column */
      int32_t  col_max = 0;               /* most positive sample in column */
      int32_t  upper, lower;
      int      top_y, bottom_y;

      /* Which ring-buffer samples belong to this pixel column */
      col_start = start + i       * available / UI_WAVEFORM_W;
      col_end   = start + (i + 1) * available / UI_WAVEFORM_W;

      for (j = col_start; j < col_end; j++)
        {
          int32_t val = (int32_t)ring_get(j);

          if (val < col_min) { col_min = val; }
          if (val > col_max) { col_max = val; }
        }

      /* Apply display-only gain & clamp */
      lower = col_min * UI_WAVEFORM_GAIN;
      upper = col_max * UI_WAVEFORM_GAIN;

      if (lower < -32767) { lower = -32767; }
      if (upper >  32767) { upper =  32767; }

      /* Map lower/upper to pixel Y (invert: 0=top, H-1=bottom) */
      top_y    = mid_y - (int)(upper * half_h / 32768);
      bottom_y = mid_y - (int)(lower * half_h / 32768);

      /* Clamp */
      if (top_y    < 0)             { top_y    = 0; }
      if (bottom_y >= UI_WAVEFORM_H){ bottom_y = UI_WAVEFORM_H - 1; }

      /* Enforce at least 1 px so silent centre-line stays visible */
      if (top_y == bottom_y)
        {
          if (bottom_y < UI_WAVEFORM_H - 1)
            {
              bottom_y = top_y + 1;
            }
          else
            {
              top_y = bottom_y - 1;
            }
        }

      /* Draw vertical bar for this column */
      line_dsc.p1.x = (int)i;
      line_dsc.p1.y = top_y;
      line_dsc.p2.x = (int)i;
      line_dsc.p2.y = bottom_y;
      lv_draw_line(&layer, &line_dsc);
    }

  /* ── 6. Flush ─────────────────────────────────────────────────── */

  lv_canvas_finish_layer(ui->waveform_canvas, &layer);
}
