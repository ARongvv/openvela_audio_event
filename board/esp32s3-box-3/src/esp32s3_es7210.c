/****************************************************************************
 * board/esp32s3-box-3/src/esp32s3_es7210.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <nuttx/i2c/i2c_master.h>

#include "esp32s3_gpio.h"
#include "esp32s3_i2c.h"
#include "esp32s3_es7210.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ES7210_RESET_REG          0x00
#define ES7210_CLOCK_OFF_REG      0x01
#define ES7210_MAINCLK_REG        0x02
#define ES7210_MSTCLK_REG         0x03
#define ES7210_LRCK_DIV_H_REG     0x04
#define ES7210_LRCK_DIV_L_REG     0x05
#define ES7210_POWER_DOWN_REG     0x06
#define ES7210_OSR_REG            0x07
#define ES7210_MODE_CONFIG_REG    0x08
#define ES7210_TIME_CONTROL0_REG  0x09
#define ES7210_TIME_CONTROL1_REG  0x0a
#define ES7210_MISC_CONTROL_REG   0x0d
#define ES7210_DMIC_CONTROL_REG   0x10
#define ES7210_SDP_CFG1_REG       0x11
#define ES7210_SDP_CFG2_REG       0x12
#define ES7210_ADC_AUTOMUTE_REG   0x13
#define ES7210_ADC34_MUTE_REG     0x14
#define ES7210_ADC12_MUTE_REG     0x15
#define ES7210_ADC4_VOL_REG       0x1b
#define ES7210_ADC3_VOL_REG       0x1c
#define ES7210_ADC2_VOL_REG       0x1d
#define ES7210_ADC1_VOL_REG       0x1e
#define ES7210_ADC1_HPF_REG       0x20
#define ES7210_ADC2_HPF_REG       0x21
#define ES7210_ADC3_HPF_REG       0x22
#define ES7210_ADC4_HPF_REG       0x23
#define ES7210_CHIP_ID1_REG       0x3d
#define ES7210_CHIP_ID0_REG       0x3e
#define ES7210_CHIP_VERSION_REG   0x3f
#define ES7210_ANALOG_REG         0x40
#define ES7210_MIC12_BIAS_REG     0x41
#define ES7210_MIC34_BIAS_REG     0x42
#define ES7210_MIC1_GAIN_REG      0x43
#define ES7210_MIC2_GAIN_REG      0x44
#define ES7210_MIC3_GAIN_REG      0x45
#define ES7210_MIC4_GAIN_REG      0x46
#define ES7210_MIC1_POWER_REG     0x47
#define ES7210_MIC2_POWER_REG     0x48
#define ES7210_MIC3_POWER_REG     0x49
#define ES7210_MIC4_POWER_REG     0x4a
#define ES7210_MIC12_POWER_REG    0x4b
#define ES7210_MIC34_POWER_REG    0x4c

#define ES7210_RESET_VALUE        0xff
#define ES7210_RESET_RELEASE      0x32
#define ES7210_RESET_CLK_OFF      0x71
#define ES7210_RESET_DEVICE_ON    0x41
#define ES7210_CHIP_ID1_VALUE     0x72
#define ES7210_CHIP_ID0_VALUE     0x10
#define ES7210_MIC_GAIN_30DB      0x0a
#define ES7210_ADC_PGA_POWER_ON   0x10
#define ES7210_MIC_POWER_ON       0x08
#define ES7210_MIC_ADC_PGA_ON     0x0f
#define ES7210_VMID_SELECT        0xc3
#define ES7210_MICBIAS_2V87       0x70
#define ES7210_MCLK_ADC_DIV1_DLL  0x81
#define ES7210_DLL_POWER_DOWN     0x04
#define ES7210_MCLK_MULTIPLE      256
#define ES7210_MUTE_STATUS_GPIO   1

struct es7210_reg_s
{
  uint8_t reg;
  uint8_t value;
};

struct es7210_dump_reg_s
{
  uint8_t reg;
  const char *name;
};

/* This sequence mirrors Apache NuttX drivers/audio/es7210.c reset flow while
 * keeping the lightweight board-local diagnostic driver used by this project.
 * It assumes MCLK = 256 * Fs and standard 16-bit I2S, non-TDM.
 */

static const struct es7210_reg_s g_es7210_init_config[] =
{
  {ES7210_TIME_CONTROL0_REG,  0x30},
  {ES7210_TIME_CONTROL1_REG,  0x30},

  {ES7210_ADC4_HPF_REG,       0x2a},
  {ES7210_ADC3_HPF_REG,       0x0a},
  {ES7210_ADC2_HPF_REG,       0x2a},
  {ES7210_ADC1_HPF_REG,       0x0a},

  /* 16-bit standard I2S, normal mode. */
  {ES7210_SDP_CFG1_REG,       0x60},
  {ES7210_SDP_CFG2_REG,       0x00},

  {ES7210_ANALOG_REG,         ES7210_VMID_SELECT},
  {ES7210_MIC12_BIAS_REG,     ES7210_MICBIAS_2V87},
  {ES7210_MIC34_BIAS_REG,     ES7210_MICBIAS_2V87},

  {ES7210_MIC1_GAIN_REG,      ES7210_MIC_GAIN_30DB |
                              ES7210_ADC_PGA_POWER_ON},
  {ES7210_MIC2_GAIN_REG,      ES7210_MIC_GAIN_30DB |
                              ES7210_ADC_PGA_POWER_ON},
  {ES7210_MIC3_GAIN_REG,      ES7210_MIC_GAIN_30DB |
                              ES7210_ADC_PGA_POWER_ON},
  {ES7210_MIC4_GAIN_REG,      ES7210_MIC_GAIN_30DB |
                              ES7210_ADC_PGA_POWER_ON},

  {ES7210_MIC1_POWER_REG,     ES7210_MIC_POWER_ON},
  {ES7210_MIC2_POWER_REG,     ES7210_MIC_POWER_ON},
  {ES7210_MIC3_POWER_REG,     ES7210_MIC_POWER_ON},
  {ES7210_MIC4_POWER_REG,     ES7210_MIC_POWER_ON},

  {ES7210_OSR_REG,            0x20},
  {ES7210_MAINCLK_REG,        ES7210_MCLK_ADC_DIV1_DLL},
  {ES7210_LRCK_DIV_H_REG,     (uint8_t)(ES7210_MCLK_MULTIPLE >> 8)},
  {ES7210_LRCK_DIV_L_REG,     (uint8_t)(ES7210_MCLK_MULTIPLE & 0xff)},

  {ES7210_POWER_DOWN_REG,     ES7210_DLL_POWER_DOWN},
  {ES7210_MIC12_POWER_REG,    ES7210_MIC_ADC_PGA_ON},
  {ES7210_MIC34_POWER_REG,    ES7210_MIC_ADC_PGA_ON},
  {ES7210_RESET_REG,          ES7210_RESET_CLK_OFF},
  {ES7210_RESET_REG,          ES7210_RESET_DEVICE_ON},
};

static const struct es7210_dump_reg_s g_es7210_dump_regs[] =
{
  {ES7210_CLOCK_OFF_REG,      "CLOCK_OFF"},
  {ES7210_CHIP_ID1_REG,       "CHIP_ID1"},
  {ES7210_CHIP_ID0_REG,       "CHIP_ID0"},
  {ES7210_CHIP_VERSION_REG,   "CHIP_VER"},
  {ES7210_MAINCLK_REG,        "MAINCLK"},
  {ES7210_MSTCLK_REG,         "MSTCLK"},
  {ES7210_LRCK_DIV_H_REG,     "LRCK_DIV_H"},
  {ES7210_LRCK_DIV_L_REG,     "LRCK_DIV_L"},
  {ES7210_POWER_DOWN_REG,     "POWER_DOWN"},
  {ES7210_OSR_REG,            "OSR"},
  {ES7210_MODE_CONFIG_REG,    "MODE_CFG"},
  {ES7210_TIME_CONTROL0_REG,  "TIME_CTRL0"},
  {ES7210_TIME_CONTROL1_REG,  "TIME_CTRL1"},
  {ES7210_MISC_CONTROL_REG,   "MISC_CTRL"},
  {ES7210_DMIC_CONTROL_REG,   "DMIC_CTRL"},
  {ES7210_SDP_CFG1_REG,       "SDP_CFG1"},
  {ES7210_SDP_CFG2_REG,       "SDP_CFG2"},
  {ES7210_ADC_AUTOMUTE_REG,   "ADC_AUTOMUTE"},
  {ES7210_ADC34_MUTE_REG,     "ADC34_MUTE"},
  {ES7210_ADC12_MUTE_REG,     "ADC12_MUTE"},
  {ES7210_ADC4_VOL_REG,       "ADC4_VOL"},
  {ES7210_ADC3_VOL_REG,       "ADC3_VOL"},
  {ES7210_ADC2_VOL_REG,       "ADC2_VOL"},
  {ES7210_ADC1_VOL_REG,       "ADC1_VOL"},
  {ES7210_ADC1_HPF_REG,       "ADC1_HPF"},
  {ES7210_ADC2_HPF_REG,       "ADC2_HPF"},
  {ES7210_ADC3_HPF_REG,       "ADC3_HPF"},
  {ES7210_ADC4_HPF_REG,       "ADC4_HPF"},
  {ES7210_ANALOG_REG,         "ANALOG"},
  {ES7210_MIC12_BIAS_REG,     "MIC12_BIAS"},
  {ES7210_MIC34_BIAS_REG,     "MIC34_BIAS"},
  {ES7210_MIC1_GAIN_REG,      "MIC1_GAIN"},
  {ES7210_MIC2_GAIN_REG,      "MIC2_GAIN"},
  {ES7210_MIC3_GAIN_REG,      "MIC3_GAIN"},
  {ES7210_MIC4_GAIN_REG,      "MIC4_GAIN"},
  {ES7210_MIC1_POWER_REG,     "MIC1_POWER"},
  {ES7210_MIC2_POWER_REG,     "MIC2_POWER"},
  {ES7210_MIC3_POWER_REG,     "MIC3_POWER"},
  {ES7210_MIC4_POWER_REG,     "MIC4_POWER"},
  {ES7210_MIC12_POWER_REG,    "MIC12_POWER"},
  {ES7210_MIC34_POWER_REG,    "MIC34_POWER"},
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int es7210_write_reg(struct i2c_master_s *i2c, uint8_t addr,
                            uint32_t frequency, uint8_t reg, uint8_t value)
{
  struct i2c_msg_s msg;
  uint8_t buffer[2];
  int ret;

  buffer[0] = reg;
  buffer[1] = value;

  msg.frequency = frequency;
  msg.addr      = addr;
  msg.flags     = 0;
  msg.buffer    = buffer;
  msg.length    = sizeof(buffer);

  ret = I2C_TRANSFER(i2c, &msg, 1);
  if (ret < 0)
    {
      auderr("ERROR: ES7210 register 0x%02x write failed: %d\n", reg, ret);
    }

  return ret;
}

static int es7210_read_reg(struct i2c_master_s *i2c, uint8_t addr,
                           uint32_t frequency, uint8_t reg, uint8_t *value)
{
  struct i2c_msg_s msgs[2];
  int ret;

  msgs[0].frequency = frequency;
  msgs[0].addr      = addr;
  msgs[0].flags     = I2C_M_NOSTOP;
  msgs[0].buffer    = &reg;
  msgs[0].length    = 1;

  msgs[1].frequency = frequency;
  msgs[1].addr      = addr;
  msgs[1].flags     = I2C_M_READ;
  msgs[1].buffer    = value;
  msgs[1].length    = 1;

  ret = I2C_TRANSFER(i2c, msgs, 2);
  if (ret < 0)
    {
      auderr("ERROR: ES7210 register 0x%02x read failed: %d\n", reg, ret);
    }

  return ret;
}

static const char *es7210_reg_name(uint8_t reg)
{
  unsigned int i;

  for (i = 0; i < sizeof(g_es7210_dump_regs) /
                  sizeof(g_es7210_dump_regs[0]); i++)
    {
      if (g_es7210_dump_regs[i].reg == reg)
        {
          return g_es7210_dump_regs[i].name;
        }
    }

  if (reg == ES7210_RESET_REG)
    {
      return "RESET";
    }

  return "UNKNOWN";
}

static int es7210_write_verify_reg(struct i2c_master_s *i2c, uint8_t addr,
                                   uint32_t frequency, uint8_t reg,
                                   uint8_t value)
{
  uint8_t readback = 0;
  const char *name;
  int ret;

  name = es7210_reg_name(reg);

  ret = es7210_write_reg(i2c, addr, frequency, reg, value);
  if (ret < 0)
    {
      printf("[es7210] write %-12s[0x%02x] want=0x%02x failed: %d\n",
             name, reg, value, ret);
      return ret;
    }

  usleep(1000);

  ret = es7210_read_reg(i2c, addr, frequency, reg, &readback);
  if (ret < 0)
    {
      printf("[es7210] verify %-11s[0x%02x] want=0x%02x read failed: %d\n",
             name, reg, value, ret);
      return ret;
    }

  printf("[es7210] write %-12s[0x%02x] want=0x%02x got=0x%02x%s\n",
         name, reg, value, readback, readback == value ? "" : " mismatch");

  return OK;
}

static void es7210_dump_registers(struct i2c_master_s *i2c, uint8_t addr,
                                  uint32_t frequency)
{
  unsigned int i;

  printf("[es7210] register readback begin\n");
  for (i = 0; i < sizeof(g_es7210_dump_regs) /
                  sizeof(g_es7210_dump_regs[0]); i++)
    {
      uint8_t value = 0;
      int ret;

      ret = es7210_read_reg(i2c, addr, frequency,
                            g_es7210_dump_regs[i].reg, &value);
      if (ret < 0)
        {
          printf("[es7210] readback %s(0x%02x) failed: %d\n",
                 g_es7210_dump_regs[i].name,
                 g_es7210_dump_regs[i].reg,
                 ret);
          continue;
        }

      printf("[es7210] reg %-12s[0x%02x] = 0x%02x\n",
             g_es7210_dump_regs[i].name,
             g_es7210_dump_regs[i].reg,
             value);
    }

  printf("[es7210] register readback end\n");
}

static void es7210_dump_mute_status(void)
{
  bool mute_status_l;
  int ret;

  ret = esp32s3_configgpio(ES7210_MUTE_STATUS_GPIO, INPUT);
  if (ret < 0)
    {
      printf("[es7210] mute status GPIO%d configure failed: %d\n",
             ES7210_MUTE_STATUS_GPIO, ret);
      return;
    }

  mute_status_l = esp32s3_gpioread(ES7210_MUTE_STATUS_GPIO);
  printf("[es7210] mute status MUTE_STATUS_L(GPIO%d)=%d %s\n",
         ES7210_MUTE_STATUS_GPIO, mute_status_l ? 1 : 0,
         mute_status_l ? "(high: not muted)" : "(low: mute active?)");
}

static int es7210_configure(struct i2c_master_s *i2c, uint8_t addr,
                            uint32_t frequency)
{
  unsigned int i;
  int ret;

  ret = es7210_write_verify_reg(i2c, addr, frequency, ES7210_RESET_REG,
                                ES7210_RESET_VALUE);
  if (ret < 0)
    {
      return ret;
    }

  usleep(5000);

  ret = es7210_write_verify_reg(i2c, addr, frequency, ES7210_RESET_REG,
                                ES7210_RESET_RELEASE);
  if (ret < 0)
    {
      return ret;
    }

  usleep(5000);

  for (i = 0; i < sizeof(g_es7210_init_config) /
                  sizeof(g_es7210_init_config[0]); i++)
    {
      ret = es7210_write_verify_reg(i2c, addr, frequency,
                                    g_es7210_init_config[i].reg,
                                    g_es7210_init_config[i].value);
      if (ret < 0)
        {
          return ret;
        }
    }

  usleep(50000);

  printf("[es7210] configured for BOX-3 ES7210 capture "
         "(NuttX reset flow, I2S slave, 16 kHz, 16-bit)\n");
  es7210_dump_registers(i2c, addr, frequency);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int esp32s3_es7210_initialize(int i2c_port, uint8_t i2c_addr,
                              uint32_t i2c_frequency)
{
  struct i2c_master_s *i2c;
  uint8_t chip_id0;
  uint8_t chip_id1;
  uint8_t chip_version;
  int ret;

  if (i2c_frequency > 400000)
    {
      auderr("ERROR: ES7210 I2C frequency too high: %lu\n",
             (unsigned long)i2c_frequency);
      return -EINVAL;
    }

  i2c = esp32s3_i2cbus_initialize(i2c_port);
  if (i2c == NULL)
    {
      auderr("ERROR: Failed to initialize I2C%d for ES7210\n", i2c_port);
      return -ENODEV;
    }

  ret = es7210_read_reg(i2c, i2c_addr, i2c_frequency,
                        ES7210_CHIP_ID1_REG, &chip_id1);
  if (ret < 0)
    {
      auderr("ERROR: ES7210 chip ID1 read failed at I2C%d address 0x%02x\n",
             i2c_port, i2c_addr);
      return ret;
    }

  ret = es7210_read_reg(i2c, i2c_addr, i2c_frequency,
                        ES7210_CHIP_ID0_REG, &chip_id0);
  if (ret < 0)
    {
      auderr("ERROR: ES7210 chip ID0 read failed at I2C%d address 0x%02x\n",
             i2c_port, i2c_addr);
      return ret;
    }

  ret = es7210_read_reg(i2c, i2c_addr, i2c_frequency,
                        ES7210_CHIP_VERSION_REG, &chip_version);
  if (ret < 0)
    {
      auderr("ERROR: ES7210 chip version read failed at I2C%d address 0x%02x\n",
             i2c_port, i2c_addr);
      return ret;
    }

  printf("[es7210] detected: chip_id1=0x%02x chip_id0=0x%02x version=0x%02x\n",
         chip_id1, chip_id0, chip_version);
  es7210_dump_mute_status();

  if (chip_id1 != ES7210_CHIP_ID1_VALUE ||
      chip_id0 != ES7210_CHIP_ID0_VALUE)
    {
      auderr("ERROR: Unexpected ES7210 chip id: 0x%02x 0x%02x\n",
             chip_id1, chip_id0);
      return -ENODEV;
    }

  return es7210_configure(i2c, i2c_addr, i2c_frequency);
}
