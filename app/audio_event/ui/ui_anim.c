/**
 * apps/examples/audio_event/ui/ui_anim.c
 *
 * Animation management: breathing LED, detection blink, bar transitions.
 */

#include "audio_event_ui.h"

/* ── Breathing LED animation ─────────────────────────────────── */

static void breathing_anim_cb(void *obj, int32_t value)
{
  lv_obj_set_style_bg_opa((lv_obj_t *)obj,
                          (lv_opa_t)value, 0);
}

static void breathing_anim_ready_cb(lv_anim_t *anim)
{
  /* Reverse the animation to create a loop */
  int32_t start = anim->start_value;
  anim->start_value = anim->current_value;
  anim->current_value = start;
  lv_anim_start(anim);
}

void ui_anim_start_breathing(audio_event_ui_t *ui)
{
  if (!ui || !ui->status_led)
    {
      return;
    }

  lv_anim_init(&ui->breathing_anim);
  lv_anim_set_var(&ui->breathing_anim, ui->status_led);
  lv_anim_set_exec_cb(&ui->breathing_anim, breathing_anim_cb);
  lv_anim_set_values(&ui->breathing_anim, LV_OPA_30, LV_OPA_COVER);
  lv_anim_set_duration(&ui->breathing_anim, UI_ANIM_BREATH_MS);
  lv_anim_set_path_cb(&ui->breathing_anim, lv_anim_path_ease_in_out);
  lv_anim_set_ready_cb(&ui->breathing_anim, breathing_anim_ready_cb);
  lv_anim_start(&ui->breathing_anim);
}

void ui_anim_stop_breathing(audio_event_ui_t *ui)
{
  if (!ui)
    {
      return;
    }

  lv_anim_delete(&ui->status_led, NULL);

  /* Restore full opacity */
  if (ui->status_led)
    {
      lv_obj_set_style_bg_opa(ui->status_led, LV_OPA_COVER, 0);
    }
}

/* ── Detection blink animation ───────────────────────────────── */

typedef struct
{
  audio_event_ui_t *ui;
  int remaining;
} blink_ctx_t;

static void blink_timer_cb(lv_timer_t *timer)
{
  blink_ctx_t *ctx = (blink_ctx_t *)timer->user_data;
  audio_event_ui_t *ui;
  lv_opa_t opa;

  if (!ctx || !ctx->ui)
    {
      lv_timer_del(timer);
      return;
    }

  ui = ctx->ui;

  if (ctx->remaining <= 0)
    {
      /* Restore normal background */
      if (ui->detection_container)
        {
          lv_obj_set_style_bg_opa(ui->detection_container,
                                  LV_OPA_COVER, 0);
        }

      lv_timer_del(timer);
      lv_free(ctx);
      return;
    }

  /* Toggle opacity */
  opa = (ctx->remaining % 2) ? LV_OPA_30 : LV_OPA_COVER;
  if (ui->detection_container)
    {
      lv_obj_set_style_bg_opa(ui->detection_container, opa, 0);
    }

  ctx->remaining--;
}

void ui_anim_blink_detection(audio_event_ui_t *ui)
{
  blink_ctx_t *ctx;
  lv_timer_t *timer;

  if (!ui || !ui->detection_container)
    {
      return;
    }

  ctx = lv_malloc(sizeof(blink_ctx_t));
  if (!ctx)
    {
      return;
    }

  ctx->ui = ui;
  ctx->remaining = UI_ANIM_BLINK_COUNT * 2;

  timer = lv_timer_create(blink_timer_cb, UI_ANIM_BLINK_MS, ctx);
  lv_timer_set_repeat_count(timer, ctx->remaining + 1);
}

/* ── Bar value transition (handled natively by lv_bar LV_ANIM_ON) */

void ui_anim_bar_transition(audio_event_ui_t *ui)
{
  /* LVGL v9 handles bar animation natively when LV_ANIM_ON is
   * passed to lv_bar_set_value(). No custom animation needed. */
  (void)ui;
}
