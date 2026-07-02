/**
 * apps/examples/audio_event/ui/ui_theme.h
 *
 * Color, font, and layout constants for the audio event detector dashboard.
 */

#pragma once

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Screen dimensions ───────────────────────────────────────── */

#define UI_SCREEN_W  320
#define UI_SCREEN_H  240

/* ── Spacing & Card geometry ─────────────────────────────────── */

#define UI_PAD_X          6
#define UI_PAD_Y          4
#define UI_GAP            6
#define UI_CARD_RADIUS    8
#define UI_CARD_BORDER_W  1

/* ── Region layout — vertical flow ──────────────────────────── */
/*  Status   :  0 ~  20  (20px)
 *  Title    : 22 ~  42  (20px)
 *  Wave+Prob: 48 ~ 148 (100px)  ← two cards side by side
 *  Detection: 154 ~ 190 (36px)  ← standalone card
 *  Params   : 196 ~ 228 (32px)  ← capsule bar at bottom              */

#define UI_STATUS_BAR_Y     0
#define UI_STATUS_BAR_H    20

#define UI_TITLE_Y         22
#define UI_TITLE_H         20

/* Waveform card (left) and probability card (right), side by side */
#define UI_WAVECARD_Y      48
#define UI_WAVECARD_H     100
#define UI_PROBCARD_Y      48
#define UI_PROBCARD_H      100

/* Inside waveform card */
#define UI_WAVEFORM_W     132
#define UI_WAVEFORM_H      84

/* Inside probability card */
#define UI_PROB_BAR_W      82
#define UI_PROB_ROW_H      20

/* Detection card (below waveform + prob) */
#define UI_DETECTCARD_Y   154
#define UI_DETECTCARD_H    36
#define UI_DETECTION_H     28
#define UI_COOLDOWN_H       6

/* Bottom params capsule */
#define UI_PARAMS_Y       196
#define UI_PARAMS_H        32

/* ── Colors — light/white theme ─────────────────────────────── */

#define UI_COLOR_BG           lv_color_hex(0xF5F5F5)
#define UI_COLOR_CARD_BG      lv_color_hex(0xFFFFFF)
#define UI_COLOR_BORDER       lv_color_hex(0xE0E0E0)
#define UI_COLOR_TEXT         lv_color_hex(0x1A1A1A)
#define UI_COLOR_TEXT_DIM     lv_color_hex(0x757575)

#define UI_COLOR_LED_GREEN    lv_color_hex(0x2ED573)
#define UI_COLOR_LED_RED      lv_color_hex(0xFF4757)
#define UI_COLOR_LED_YELLOW   lv_color_hex(0xFFA502)

#define UI_COLOR_ACCENT       lv_color_hex(0x1565C0)
#define UI_COLOR_BAR_ACTIVE   lv_color_hex(0x1565C0)
#define UI_COLOR_BAR_INACTIVE lv_color_hex(0xE0E0E0)
#define UI_COLOR_BAR_BG       lv_color_hex(0xEEEEEE)

#define UI_COLOR_KNOCK        lv_color_hex(0xE65100)
#define UI_COLOR_COUGH        lv_color_hex(0x7B1FA2)
#define UI_COLOR_BACKGROUND   lv_color_hex(0x78909C)
#define UI_COLOR_SILENCE      lv_color_hex(0x90A4AE)

#define UI_COLOR_WAVEFORM     lv_color_hex(0x1565C0)
#define UI_COLOR_WAVEFORM_BG  lv_color_hex(0xFFFFFF)

/* ── Fonts (binary bpp=1, no anti-aliasing artifacts on RGB565) ── */

extern const lv_font_t lv_font_montserrat_20_bin;
extern const lv_font_t lv_font_montserrat_14_bin;

#define UI_FONT_TITLE     &lv_font_montserrat_20_bin
#define UI_FONT_NORMAL    &lv_font_montserrat_20_bin
#define UI_FONT_SMALL     &lv_font_montserrat_20_bin
#define UI_FONT_TINY      &lv_font_montserrat_14_bin
#define UI_FONT_BIG       &lv_font_montserrat_20_bin

/* ── Class names (English, no Chinese glyphs needed) ─────────── */

#define UI_CLASS_NAME_KNOCK      "Knock"
#define UI_CLASS_NAME_COUGH      "Cough"
#define UI_CLASS_NAME_BACKGROUND "Bgnd"
#define UI_CLASS_NAME_SILENCE    "Quiet"

/* ── Animation durations (ms) ────────────────────────────────── */

#define UI_ANIM_BAR_MS       200
#define UI_ANIM_FADE_MS      300
#define UI_ANIM_BREATH_MS   1000
#define UI_ANIM_BLINK_MS     150
#define UI_ANIM_BLINK_COUNT    3

/* ── UI state enum ───────────────────────────────────────────── */

enum ui_state_e
{
  UI_STATE_LISTENING = 0,
  UI_STATE_TRIGGERED,
  UI_STATE_COOLDOWN
};

#ifdef __cplusplus
}
#endif
