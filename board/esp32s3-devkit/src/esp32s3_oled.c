/****************************************************************************
 * boards/xtensa/esp32s3/esp32s3-devkit/src/esp32s3_oled.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <debug.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/ssd1306.h>

#include "esp32s3_i2c.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define OLED_I2C_PORT ESP32S3_I2C0
#define OLED_XRES     128
#define OLED_YRES     64
#define OLED_ROWBYTES 16

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct i2c_master_s *g_i2cdev;
static struct lcd_dev_s *g_lcddev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static const uint8_t *oled_glyph(char ch)
{
  static const uint8_t space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t zero[5]  = {0x3e, 0x51, 0x49, 0x45, 0x3e};
  static const uint8_t two[5]   = {0x42, 0x61, 0x51, 0x49, 0x46};
  static const uint8_t three[5] = {0x21, 0x41, 0x45, 0x4b, 0x31};
  static const uint8_t a[5]     = {0x7e, 0x11, 0x11, 0x11, 0x7e};
  static const uint8_t c[5]     = {0x3e, 0x41, 0x41, 0x41, 0x22};
  static const uint8_t d[5]     = {0x7f, 0x41, 0x41, 0x22, 0x1c};
  static const uint8_t e[5]     = {0x7f, 0x49, 0x49, 0x49, 0x41};
  static const uint8_t f[5]     = {0x7f, 0x09, 0x09, 0x09, 0x01};
  static const uint8_t i[5]     = {0x00, 0x41, 0x7f, 0x41, 0x00};
  static const uint8_t k[5]     = {0x7f, 0x08, 0x14, 0x22, 0x41};
  static const uint8_t l[5]     = {0x7f, 0x40, 0x40, 0x40, 0x40};
  static const uint8_t o[5]     = {0x3e, 0x41, 0x41, 0x41, 0x3e};
  static const uint8_t u[5]     = {0x3f, 0x40, 0x40, 0x40, 0x3f};
  static const uint8_t x[5]     = {0x63, 0x14, 0x08, 0x14, 0x63};

  switch (ch)
    {
      case '0':
        return zero;
      case '2':
        return two;
      case '3':
        return three;
      case 'A':
        return a;
      case 'C':
        return c;
      case 'D':
        return d;
      case 'E':
        return e;
      case 'F':
        return f;
      case 'I':
        return i;
      case 'K':
        return k;
      case 'L':
        return l;
      case 'O':
        return o;
      case 'U':
        return u;
      case 'X':
      case 'x':
        return x;
      default:
        return space;
    }
}

static void oled_set_pixel(uint8_t row[OLED_ROWBYTES], unsigned int x)
{
  if (x < OLED_XRES)
    {
      row[x >> 3] |= 1 << (x & 7);
    }
}

static int oled_put_text_line(unsigned int row, unsigned int col,
                              const char *text)
{
  struct lcd_planeinfo_s pinfo;
  uint8_t line[7][OLED_ROWBYTES];
  unsigned int i;
  int ret;

  if (g_lcddev == NULL || text == NULL || row + 7 > OLED_YRES)
    {
      return -EINVAL;
    }

  ret = g_lcddev->getplaneinfo(g_lcddev, 0, &pinfo);
  if (ret < 0)
    {
      return ret;
    }

  memset(line, 0, sizeof(line));

  while (*text != '\0' && col + 5 < OLED_XRES)
    {
      const uint8_t *glyph = oled_glyph(*text++);
      unsigned int gx;

      for (gx = 0; gx < 5; gx++)
        {
          unsigned int gy;

          for (gy = 0; gy < 7; gy++)
            {
              if ((glyph[gx] & (1 << gy)) != 0)
                {
                  oled_set_pixel(line[gy], col + gx);
                }
            }
        }

      col += 6;
    }

  for (i = 0; i < 7; i++)
    {
      ret = pinfo.putrun(g_lcddev, row + i, 0, line[i], OLED_XRES);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

static void oled_splash(void)
{
  int ret;

  ret = ssd1306_fill(g_lcddev, SSD1306_Y1_BLACK);
  if (ret < 0)
    {
      lcderr("ERROR: Failed to clear OLED: %d\n", ret);
      return;
    }

  oled_put_text_line(8, 10, "CCF AUDIO");
  oled_put_text_line(24, 10, "OLED OK");
  oled_put_text_line(40, 10, "I2C 0x3C");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int board_lcd_initialize(void)
{
  g_i2cdev = esp32s3_i2cbus_initialize(OLED_I2C_PORT);
  if (g_i2cdev == NULL)
    {
      lcderr("ERROR: Failed to initialize I2C port %d\n", OLED_I2C_PORT);
      return -ENODEV;
    }

  g_lcddev = ssd1306_initialize(g_i2cdev, NULL, 0);
  if (g_lcddev == NULL)
    {
      lcderr("ERROR: Failed to bind I2C port %d to SSD1306 OLED\n",
             OLED_I2C_PORT);
      return -ENODEV;
    }

  g_lcddev->setpower(g_lcddev, CONFIG_LCD_MAXPOWER);
  oled_splash();
  lcdinfo("I2C port %d bound to SSD1306 OLED\n", OLED_I2C_PORT);

  return OK;
}

struct lcd_dev_s *board_lcd_getdev(int devno)
{
  if (devno == 0)
    {
      return g_lcddev;
    }

  return NULL;
}

void board_lcd_uninitialize(void)
{
  if (g_lcddev != NULL)
    {
      g_lcddev->setpower(g_lcddev, 0);
    }
}
