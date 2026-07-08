/*
 * Compact monochrome 128x64 OLED UI for audio_event.
 */

#include "ui_oled/audio_event_oled_ui.h"

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/lcd/lcd_dev.h>

#include "audio_event_config.h"

#ifndef CONFIG_EXAMPLES_AUDIO_EVENT_OLED_DEVPATH
#  define CONFIG_EXAMPLES_AUDIO_EVENT_OLED_DEVPATH "/dev/lcd0"
#endif

#define OLED_W        128
#define OLED_H         64
#define OLED_ROWBYTES  16
#define OLED_BAR_W    100

enum oled_state_e
{
  OLED_STATE_LISTENING = 0,
  OLED_STATE_TRIGGERED,
  OLED_STATE_COOLDOWN
};

static int g_fd = -1;
static bool g_active;
static int g_state;
static int g_last_class = AUDIO_EVENT_CLASS_SILENCE;
static int g_last_confidence;
static uint32_t g_cooldown_ms;
static uint8_t g_fb[OLED_H][OLED_ROWBYTES];

static const char *const g_class_names[AUDIO_EVENT_CLASS_COUNT] =
{
  "KNOCK",
  "COUGH",
  "BGND",
  "QUIET"
};

static const uint8_t *oled_glyph(char ch)
{
  static const uint8_t glyphs[][5] =
  {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x00, 0x00, 0x5f, 0x00, 0x00}, /* ! */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* % */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, /* A */
    {0x7f, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3e, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7f, 0x41, 0x41, 0x22, 0x1c}, /* D */
    {0x7f, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7f, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, /* G */
    {0x7f, 0x08, 0x08, 0x08, 0x7f}, /* H */
    {0x00, 0x41, 0x7f, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3f, 0x01}, /* J */
    {0x7f, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7f, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, /* M */
    {0x7f, 0x04, 0x08, 0x10, 0x7f}, /* N */
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, /* O */
    {0x7f, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3e, 0x41, 0x51, 0x21, 0x5e}, /* Q */
    {0x7f, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7f, 0x01, 0x01}, /* T */
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, /* U */
    {0x1f, 0x20, 0x40, 0x20, 0x1f}, /* V */
    {0x3f, 0x40, 0x38, 0x40, 0x3f}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, /* 0 */
    {0x00, 0x42, 0x7f, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4b, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7f, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1e}, /* 9 */
  };

  if (ch >= 'a' && ch <= 'z')
    {
      ch -= 'a' - 'A';
    }

  if (ch == ' ')
    {
      return glyphs[0];
    }
  else if (ch == '!')
    {
      return glyphs[1];
    }
  else if (ch == '%')
    {
      return glyphs[2];
    }
  else if (ch == '-')
    {
      return glyphs[3];
    }
  else if (ch == '.')
    {
      return glyphs[4];
    }
  else if (ch == '/')
    {
      return glyphs[5];
    }
  else if (ch == ':')
    {
      return glyphs[6];
    }
  else if (ch >= 'A' && ch <= 'Z')
    {
      return glyphs[7 + ch - 'A'];
    }
  else if (ch >= '0' && ch <= '9')
    {
      return glyphs[33 + ch - '0'];
    }

  return glyphs[0];
}

static void oled_clear(void)
{
  memset(g_fb, 0, sizeof(g_fb));
}

static void oled_set_pixel(int x, int y)
{
  if (x >= 0 && x < OLED_W && y >= 0 && y < OLED_H)
    {
      x = OLED_W - 1 - x;

#ifdef CONFIG_LCD_PACKEDMSFIRST
      g_fb[y][x >> 3] |= 0x80 >> (x & 7);
#else
      g_fb[y][x >> 3] |= 1 << (x & 7);
#endif
    }
}

static void oled_fill_rect(int x, int y, int w, int h)
{
  int yy;
  int xx;

  for (yy = y; yy < y + h; yy++)
    {
      for (xx = x; xx < x + w; xx++)
        {
          oled_set_pixel(xx, yy);
        }
    }
}

static int oled_text_width(const char *text, int scale)
{
  int len = 0;

  while (text != NULL && text[len] != '\0')
    {
      len++;
    }

  return len == 0 ? 0 : len * 6 * scale - scale;
}

static void oled_draw_char(int x, int y, char ch, int scale)
{
  const uint8_t *glyph = oled_glyph(ch);
  int gx;

  for (gx = 0; gx < 5; gx++)
    {
      int gy;

      for (gy = 0; gy < 7; gy++)
        {
          if ((glyph[gx] & (1 << gy)) != 0)
            {
              oled_fill_rect(x + gx * scale, y + gy * scale,
                             scale, scale);
            }
        }
    }
}

static void oled_draw_text(int x, int y, const char *text, int scale)
{
  while (text != NULL && *text != '\0')
    {
      oled_draw_char(x, y, *text++, scale);
      x += 6 * scale;
    }
}

static void oled_draw_centered(int y, const char *text, int scale)
{
  int x = (OLED_W - oled_text_width(text, scale)) / 2;

  if (x < 0)
    {
      x = 0;
    }

  oled_draw_text(x, y, text, scale);
}

static void oled_draw_bar(int x, int y, int w, int h, int permille)
{
  int fill;

  if (permille < 0)
    {
      permille = 0;
    }
  else if (permille > 1000)
    {
      permille = 1000;
    }

  fill = (w - 2) * permille / 1000;
  oled_fill_rect(x, y, w, 1);
  oled_fill_rect(x, y + h - 1, w, 1);
  oled_fill_rect(x, y, 1, h);
  oled_fill_rect(x + w - 1, y, 1, h);
  if (fill > 0)
    {
      oled_fill_rect(x + 1, y + 1, fill, h - 2);
    }
}

static int oled_flush(void)
{
  struct lcddev_run_s run;
  int row;
  int ret;

  if (g_fd < 0)
    {
      return -ENODEV;
    }

  for (row = 0; row < OLED_H; row++)
    {
      run.row = row;
      run.col = 0;
      run.data = g_fb[row];
      run.npixels = OLED_W;
      ret = ioctl(g_fd, LCDDEVIO_PUTRUN, (unsigned long)(uintptr_t)&run);
      if (ret < 0)
        {
          return -errno;
        }
    }

  return 0;
}

static int oled_percent(int confidence)
{
  if (confidence < 0)
    {
      confidence = 0;
    }
  else if (confidence > 1000)
    {
      confidence = 1000;
    }

  return (confidence + 5) / 10;
}

static int oled_volume_permille(uint32_t rms)
{
  uint32_t scaled = rms / 16;

  if (scaled > 1000)
    {
      scaled = 1000;
    }

  return (int)scaled;
}

static void oled_draw_listening(int class_id, int confidence, uint32_t rms)
{
  char line[24];

  if (class_id < 0 || class_id >= AUDIO_EVENT_CLASS_COUNT)
    {
      class_id = AUDIO_EVENT_CLASS_SILENCE;
    }

  oled_clear();
  oled_draw_centered(0, "AUDIO EVENT", 1);

  if (class_id == AUDIO_EVENT_CLASS_SILENCE)
    {
      oled_draw_centered(15, "QUIET", 2);
    }
  else
    {
      oled_draw_centered(15, g_class_names[class_id], 2);
    }

  snprintf(line, sizeof(line), "CONF %3d%%", oled_percent(confidence));
  oled_draw_centered(36, line, 1);
  snprintf(line, sizeof(line), "RMS %5lu", (unsigned long)rms);
  oled_draw_text(6, 48, line, 1);
  oled_draw_bar(22, 58, OLED_BAR_W, 6, oled_volume_permille(rms));
  oled_flush();
}

static void oled_draw_detection(int class_id, int confidence)
{
  char line[24];

  if (class_id < 0 || class_id >= AUDIO_EVENT_CLASS_COUNT)
    {
      return;
    }

  oled_clear();
  snprintf(line, sizeof(line), "%s!", g_class_names[class_id]);
  oled_draw_centered(8, line, 2);
  snprintf(line, sizeof(line), "CONF %d", confidence);
  oled_draw_centered(34, line, 1);
  snprintf(line, sizeof(line), "HIT %d/%d",
           CONFIG_EXAMPLES_AUDIO_EVENT_CONSECUTIVE_HITS,
           CONFIG_EXAMPLES_AUDIO_EVENT_CONSECUTIVE_HITS);
  oled_draw_centered(48, line, 1);
  oled_flush();
}

static void oled_draw_cooldown(uint32_t remaining_ms)
{
  char line[24];
  int permille = 0;

  oled_clear();
  oled_draw_centered(0, "COOLDOWN", 1);
  snprintf(line, sizeof(line), "%lu.%lus",
           (unsigned long)(remaining_ms / 1000),
           (unsigned long)((remaining_ms % 1000) / 100));
  oled_draw_centered(14, line, 2);

  snprintf(line, sizeof(line), "LAST %s", g_class_names[g_last_class]);
  oled_draw_centered(42, line, 1);

  if (g_cooldown_ms > 0)
    {
      permille = (int)((uint64_t)remaining_ms * 1000 / g_cooldown_ms);
    }

  oled_draw_bar(14, 56, 100, 6, permille);
  oled_flush();
}

int audio_event_oled_ui_init(void)
{
  if (g_active)
    {
      return 0;
    }

  g_fd = open(CONFIG_EXAMPLES_AUDIO_EVENT_OLED_DEVPATH, O_RDWR | O_CLOEXEC);
  if (g_fd < 0)
    {
      int ret = -errno;
      printf("[audio_event_oled] open %s failed: %d\n",
             CONFIG_EXAMPLES_AUDIO_EVENT_OLED_DEVPATH, ret);
      return ret;
    }

  g_active = true;
  g_state = OLED_STATE_LISTENING;
  g_last_class = AUDIO_EVENT_CLASS_SILENCE;
  g_last_confidence = 0;
  g_cooldown_ms = 0;
  oled_draw_listening(AUDIO_EVENT_CLASS_SILENCE, 0, 0);
  printf("[audio_event_oled] init OK dev=%s\n",
         CONFIG_EXAMPLES_AUDIO_EVENT_OLED_DEVPATH);
  return 0;
}

void audio_event_oled_ui_deinit(void)
{
  if (g_fd >= 0)
    {
      close(g_fd);
      g_fd = -1;
    }

  g_active = false;
}

bool audio_event_oled_ui_is_active(void)
{
  return g_active;
}

void audio_event_oled_ui_update_audio(int class_id, int confidence,
                                      uint32_t rms)
{
  if (!g_active || g_state != OLED_STATE_LISTENING)
    {
      return;
    }

  oled_draw_listening(class_id, confidence, rms);
}

void audio_event_oled_ui_notify_detection(int class_id, int confidence)
{
  if (!g_active)
    {
      return;
    }

  if (class_id >= 0 && class_id < AUDIO_EVENT_CLASS_COUNT)
    {
      g_last_class = class_id;
      g_last_confidence = confidence;
    }

  g_state = OLED_STATE_TRIGGERED;
  oled_draw_detection(g_last_class, g_last_confidence);
}

void audio_event_oled_ui_notify_cooldown(uint32_t duration_ms)
{
  if (!g_active)
    {
      return;
    }

  g_cooldown_ms = duration_ms;
}

void audio_event_oled_ui_update_cooldown(uint32_t remaining_ms)
{
  if (!g_active || g_cooldown_ms == 0)
    {
      return;
    }

  if (g_state == OLED_STATE_TRIGGERED &&
      remaining_ms + 1000 > g_cooldown_ms)
    {
      return;
    }

  g_state = OLED_STATE_COOLDOWN;
  oled_draw_cooldown(remaining_ms);
}

void audio_event_oled_ui_set_listening(void)
{
  if (!g_active)
    {
      return;
    }

  g_state = OLED_STATE_LISTENING;
  g_cooldown_ms = 0;
  oled_draw_listening(AUDIO_EVENT_CLASS_SILENCE, 0, 0);
}
