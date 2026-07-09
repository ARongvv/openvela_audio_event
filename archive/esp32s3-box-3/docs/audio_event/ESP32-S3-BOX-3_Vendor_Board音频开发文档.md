# ESP32-S3-BOX-3 Vendor Board 音频开发文档

> 版本：1.0
> 更新日期：2026-06-24
> 适用范围：`contest2026_031_niudanxianqianchong` 项目 ESP32-S3-BOX-3 板级音频移植
> 麦克风方案：方案 A（ES7210 I2C 初始化 + audio_i2s 通用适配层）

## 1. 文档目标

本文档说明如何在不修改 NuttX 源仓库代码的前提下，为 ESP32-S3-BOX-3 实现
麦克风音频采集，使 `audio_event` 应用能够从 `/dev/audio/pcm_in0` 读取 PCM
数据。

核心思路：使用 `CONFIG_ARCH_BOARD_CUSTOM=y` 机制，在比赛仓库的 vendor 目录
中提供完整的自定义 board 代码（参考 esp32s3-eye 的实现模式），在 bringup 中
增加 ES7210 I2C 初始化和 I2S RX 注册。

## 2. ESP32-S3-BOX-3 音频硬件链路

### 2.1 硬件架构

```text
双 MEMS 麦克风 (MIC1 + MIC2)
    ↓ 模拟信号
ES7210 (4 通道 ADC, I2C 地址 0x40)
    ↓ I2S 数字音频
GPIO16 (ADC_SDOUT) → ESP32-S3 I2S0 RX
    ↓
ESP32-S3 I2S0 TX
    ↓ GPIO15 (CODEC_DSDIN)
ES8311 (DAC/播放 Codec, I2C 地址 0x18)
    ↓ 模拟输出
功放 (PA_CTRL=GPIO46)
    ↓
扬声器
```

### 2.2 GPIO 引脚分配

| 信号 | GPIO | 说明 |
|---|---|---|
| I2S MCLK | 2 | 主时钟，ES7210 和 ES8311 共用 |
| I2S BCLK/SCLK | 17 | 位时钟 |
| I2S LRCK/WS | 45 | 字选择（帧同步） |
| I2S ADC_SDOUT | 16 | ES7210 数据输出（录音） |
| I2S CODEC_DSDIN | 15 | ES8311 数据输入（播放） |
| I2C SCL | 18 | I2C 时钟（ES7210 + ES8311 + GT911 共用） |
| I2C SDA | 8 | I2C 数据 |
| PA_CTRL | 46 | 功放使能（仅播放时需要） |
| 静音按钮 | - | 硬件静音：切断 ADC 电源并禁用 ADC_SDOUT |

### 2.3 注意事项

- 静音按钮按下时，硬件会切断 ES7210 电源并禁用 ADC_SDOUT。黄色静音灯亮
  时，即使软件配置正确也采不到声音。
- ES8311 的模拟麦克风输入仅引到测试点 TP7/TP8，不连接板载双麦克风。
- ES7210 输出双通道数据（MIC1 + MIC2），audio_event 需要单声道。

## 3. 整体方案

### 3.1 为什么需要自定义 board

NuttX 上游的 esp32s3-box board 代码（`nuttx/boards/xtensa/esp32s3/esp32s3-box/`）
的 `esp32s3_bringup()` 中没有音频初始化代码。要添加音频支持，有两种方式：

| 方式 | 做法 | 是否修改 NuttX |
|---|---|---|
| 直接改 NuttX | 修改 `nuttx/boards/.../esp32s3_bringup.c` | 是 |
| ARCH_BOARD_CUSTOM | 在 vendor 目录提供 esp32s3-box-3 完整 board 代码 | 否 |

本文档采用后者，在比赛仓库中创建 `esp32s3-box-3` 自定义 board，并映射到
`vendor/espressif/boards/esp32s3/esp32s3-box-3/`。该位置与
`esp32s3-eye` 同级，可以继续复用 NuttX 的 ESP32-S3 common board 代码。

### 3.2 为什么用方案 A（audio_i2s）

NuttX 仓库中没有 ES7210 驱动。两种实现方式：

| 方案 | 描述 | 工作量 |
|---|---|---|
| A：audio_i2s 通用层 | I2C 一次性配置 ES7210 + `board_i2sdev_initialize()` | ~200 行 |
| B：完整 ES7210 lower-half | 实现 `audio_ops_s` 全部回调 | ~1000+ 行 |

方案 A 利用 NuttX 已有的 `audio_i2s` 适配层自动管理 DMA 和 buffer，
只需在启动时通过 I2C 将 ES7210 配置为正确的工作模式。

### 3.3 启动时序

```text
board_late_initialize()
  └→ esp32s3_bringup()
       ├→ procfs, tmpfs, timers, watchdog, WiFi ...  (原有代码)
       ├→ board_i2c_init()                           (I2C 总线)
       ├→ esp32s3_es7210_initialize()                 (新增: I2C 配置 ES7210)
       └→ board_i2sdev_initialize(I2S0, false, true) (新增: 注册 I2S RX 设备)
            └→ 创建 /dev/audio/pcm_in0

audio_event_main()
  └→ open("/dev/audio/pcm_in0") → 读取 PCM 数据
```

## 4. 目录结构

在比赛仓库中创建以下目录，通过 manifest linkfile 映射到构建系统：

```text
contest2026_031_niudanxianqianchong/
└── board/
    └── esp32s3-box-3/
        ├── Kconfig                           # 定义 ESP32S3_BOX_AUDIO 选项
        ├── configs/
        │   └── audio_event/
        │       └── defconfig                 # ARCH_BOARD_CUSTOM=y + 音频配置
        ├── include/
        │   ├── board.h                       # 从 NuttX esp32s3-box 复制
        │   └── board_memorymap.h             # 从 NuttX esp32s3-box 复制
        ├── scripts/
        │   └── Make.defs                     # 从 NuttX esp32s3-box 复制
        └── src/
            ├── Make.defs                     # 修改：加入 es7210 初始化源文件
            ├── esp32s3-box-3.h               # 修改：加入 ES7210 GPIO 定义
            ├── esp32s3_boot.c                # 从 NuttX esp32s3-box 复制（不改）
            ├── esp32s3_bringup.c             # 修改：加入音频初始化
            ├── esp32s3_appinit.c             # 从 NuttX esp32s3-box 复制（不改）
            ├── esp32s3_reset.c               # 从 NuttX esp32s3-box 复制（不改）
            ├── esp32s3_buttons.c             # 从 NuttX esp32s3-box 复制（不改）
            ├── esp32s3_board_spi.c           # 从 NuttX esp32s3-box 复制（不改）
            ├── esp32s3_board_lcd_ili9342c.c  # 从 NuttX esp32s3-box 复制（不改）
            ├── esp32s3_board_touchsceen_gt911.c # 从 NuttX esp32s3-box 复制（不改）
            └── esp32s3_es7210.c              # 新增：ES7210 I2C 初始化
```

注：`include/`、`scripts/` 和不修改的 `src/` 文件直接从 NuttX 上游
`nuttx/boards/xtensa/esp32s3/esp32s3-box/` 复制，仅 `src/` 中标注"修改"和
"新增"的文件需要改动。

manifest linkfile（需要新增）：

```xml
<linkfile src="board/esp32s3-box-3"
          dest="vendor/espressif/boards/esp32s3/esp32s3-box-3"/>
```

## 5. 文件变更详解

### 5.1 Kconfig（新增文件）

```kconfig
if ARCH_BOARD_CUSTOM

config ESP32S3_BOX_AUDIO
    bool "Enable ESP32-S3-BOX-3 Audio (ES7210 microphone)"
    default n
    select ESP32S3_I2S
    select ESP32S3_I2S0
    select ESP32S3_I2S0_RX
    select ESP32S3_I2S0_MCLK
    select ESP32S3_I2C0
    select AUDIO
    select AUDIO_I2S
    select DRIVERS_AUDIO
    ---help---
        Enable the ES7210 microphone ADC and I2S RX path on ESP32-S3-BOX-3.
        Registers /dev/audio/pcm_in0 for 16-bit PCM capture.

endif # ARCH_BOARD_CUSTOM
```

### 5.2 esp32s3-box-3.h（修改）

在文件末尾 `#endif` 之前增加 ES7210 相关定义：

```c
/* ES7210 Microphone ADC (ESP32-S3-BOX-3) */

#ifdef CONFIG_ESP32S3_BOX_AUDIO

#  define ES7210_I2C_PORT     0
#  define ES7210_I2C_ADDR     0x40
#  define ES7210_I2C_FREQ     100000

/* Audio is on I2S0, shared with ES8311 */

#  define AUDIO_I2S_PORT      ESP32S3_I2S0

#endif /* CONFIG_ESP32S3_BOX_AUDIO */
```

### 5.3 esp32s3_bringup.c（修改）

在 `#ifdef CONFIG_ESP32S3_BOX_LCD` 之前增加：

```c
#ifdef CONFIG_ESP32S3_I2S
#  include "esp32s3_i2s.h"
#endif

#ifdef CONFIG_ESP32S3_BOX_AUDIO
#  include "esp32s3_es7210.h"
#endif
```

在 `esp32s3_bringup()` 函数体中，LCD 初始化之前增加：

```c
#ifdef CONFIG_ESP32S3_BOX_AUDIO

  /* Initialize ES7210 microphone ADC via I2C */

  ret = esp32s3_es7210_initialize(ES7210_I2C_PORT, ES7210_I2C_ADDR,
                                  ES7210_I2C_FREQ);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to initialize ES7210: %d\n", ret);
    }

  /* Register I2S0 RX device as /dev/audio/pcm_in0 */

  ret = board_i2sdev_initialize(AUDIO_I2S_PORT, false, true);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to initialize I2S0 RX: %d\n", ret);
    }

#endif /* CONFIG_ESP32S3_BOX_AUDIO */
```

### 5.4 esp32s3_es7210.c（新增文件）

这是核心新增文件，通过 I2C 配置 ES7210 寄存器。

```c
/****************************************************************************
 * boards/xtensa/esp32s3/esp32s3-box-3/src/esp32s3_es7210.c
 *
 * ES7210 四通道 ADC 初始化（方案 A：一次性 I2C 寄存器配置）。
 * 不实现 NuttX Audio lower-half，仅配置芯片工作模式。
 * 音频数据通过 audio_i2s 通用适配层经 I2S RX 传入。
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdio.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/kmalloc.h>

#include "esp32s3_i2c.h"

/****************************************************************************
 * ES7210 寄存器定义
 *
 * 基于 ES7210 数据手册，仅列出初始化所需的寄存器。
 * I2C 地址: 7-bit 0x40 (硬件 ADR 引脚决定)
 ****************************************************************************/

#define ES7210_RESET_REG        0x00
#define ES7210_CHIP_ID_REG      0xFD
#define ES7210_CLOCK_OFF_REG    0x01
#define ES7210_MAINCLK_REG      0x02
#define ES7210_MSTCLK_REG       0x03
#define ES7210_LRCK_DIV_H_REG   0x04
#define ES7210_LRCK_DIV_L_REG   0x05
#define ES7210_FS_DIV_REG       0x06
#define ES7210_SDP_REG1         0x09
#define ES7210_SDP_REG2         0x0A
#define ES7210_ADC_AUTOMUTE_REG 0x0D
#define ES7210_ADC12_MUTE_REG   0x14
#define ES7210_ADC34_MUTE_REG   0x15
#define ES7210_ADC1_HPF_REG     0x20
#define ES7210_ADC1_SRC_REG     0x21
#define ES7210_ADC1_VOL_REG     0x24
#define ES7210_ADC2_SRC_REG     0x25
#define ES7210_ADC2_VOL_REG     0x28
#define ES7210_ADC3_SRC_REG     0x29
#define ES7210_ADC3_VOL_REG     0x2C
#define ES7210_ADC4_SRC_REG     0x2D
#define ES7210_ADC4_VOL_REG     0x30
#define ES7210_PGA_CTRL_REG     0x40
#define ES7210_PGA_GAIN1_REG    0x41
#define ES7210_PGA_GAIN2_REG    0x42
#define ES7210_PGA_GAIN3_REG    0x43
#define ES7210_PGA_GAIN4_REG    0x44
#define ES7210_POWERUP_REG      0x11

/* 芯片复位值 */

#define ES7210_RESET_VALUE      0xFF

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int es7210_write_reg(FAR struct i2c_master_s *i2c, uint8_t addr,
                            uint8_t reg, uint8_t value)
{
  struct i2c_msg_s msg;
  uint8_t buf[2];
  int ret;

  buf[0] = reg;
  buf[1] = value;

  msg.frequency = 100000;
  msg.addr      = addr;
  msg.flags     = 0;
  msg.buffer    = buf;
  msg.length    = 2;

  ret = i2c_transfer(i2c, &msg, 1);
  if (ret < 0)
    {
      auderr("ES7210 write reg 0x%02x failed: %d\n", reg, ret);
    }

  return ret;
}

static int es7210_read_reg(FAR struct i2c_master_s *i2c, uint8_t addr,
                           uint8_t reg, FAR uint8_t *value)
{
  struct i2c_msg_s msgs[2];
  int ret;

  msgs[0].frequency = 100000;
  msgs[0].addr      = addr;
  msgs[0].flags     = I2C_M_NOSTOP;
  msgs[0].buffer    = &reg;
  msgs[0].length    = 1;

  msgs[1].frequency = 100000;
  msgs[1].addr      = addr;
  msgs[1].flags     = I2C_M_READ;
  msgs[1].buffer    = value;
  msgs[1].length    = 1;

  ret = i2c_transfer(i2c, msgs, 2);
  if (ret < 0)
    {
      auderr("ES7210 read reg 0x%02x failed: %d\n", reg, ret);
    }

  return ret;
}

/****************************************************************************
 * Name: es7210_configure
 *
 * Description:
 *   Configure ES7210 for 16 kHz, 16-bit, I2S slave mode, dual channel
 *   (MIC1 + MIC2).
 *
 *   寄存器配置说明：
 *   - 复位芯片
 *   - 设置时钟: MCLK/LRCK 比率，采样率分频
 *   - 设置 I2S 格式: 16-bit, I2S standard, slave mode
 *   - 选择 ADC 输入源: MIC1 -> ADC1, MIC2 -> ADC2
 *   - 设置 PGA 增益
 *   - 启用 ADC1 和 ADC2，禁用 ADC3 和 ADC4
 *   - 去除自动静音
 *
 *   注意: 以下寄存器值基于 ES7210 数据手册和 BOX-3 硬件参考设计，
 *   可能需要根据实际测试调整。
 ****************************************************************************/

static int es7210_configure(FAR struct i2c_master_s *i2c, uint8_t addr)
{
  int ret;

  /* Step 1: 软复位 */

  ret = es7210_write_reg(i2c, addr, ES7210_RESET_REG, ES7210_RESET_VALUE);
  if (ret < 0)
    {
      return ret;
    }

  /* 等待复位完成 */

  usleep(5000);

  /* Step 2: 时钟配置
   *
   * MCLK 源: 外部 MCLK (GPIO2)
   * MCLK/LRCK = 256 (16 kHz 采样率时 MCLK = 4.096 MHz)
   * BCLK/LRCK = 64  (16-bit, 双通道)
   */

  ret = es7210_write_reg(i2c, addr, ES7210_MAINCLK_REG, 0x00);
  if (ret < 0) return ret;

  ret = es7210_write_reg(i2c, addr, ES7210_MSTCLK_REG, 0x02);
  if (ret < 0) return ret;

  /* LRCK 分频 = 256 -> reg = 0x0100 (高 8 位 + 低 8 位) */

  ret = es7210_write_reg(i2c, addr, ES7210_LRCK_DIV_H_REG, 0x01);
  if (ret < 0) return ret;

  ret = es7210_write_reg(i2c, addr, ES7210_LRCK_DIV_L_REG, 0x00);
  if (ret < 0) return ret;

  /* Step 3: I2S 格式配置
   *
   * SDP_REG1: I2S format, 16-bit word length
   * SDP_REG2: Slave mode, BCLK normal polarity
   */

  ret = es7210_write_reg(i2c, addr, ES7210_SDP_REG1, 0x00);
  if (ret < 0) return ret;

  ret = es7210_write_reg(i2c, addr, ES7210_SDP_REG2, 0x00);
  if (ret < 0) return ret;

  /* Step 4: ADC 输入源选择
   *
   * ADC1 源 = MIC1 (正极)
   * ADC2 源 = MIC2 (正极)
   * ADC3/ADC4 禁用
   */

  ret = es7210_write_reg(i2c, addr, ES7210_ADC1_SRC_REG, 0x00);
  if (ret < 0) return ret;

  ret = es7210_write_reg(i2c, addr, ES7210_ADC2_SRC_REG, 0x00);
  if (ret < 0) return ret;

  /* Step 5: PGA 增益配置
   *
   * PGA_GAIN1: MIC1 增益 0 dB
   * PGA_GAIN2: MIC2 增益 0 dB
   * PGA_GAIN3/4: 禁用
   */

  ret = es7210_write_reg(i2c, addr, ES7210_PGA_GAIN1_REG, 0x00);
  if (ret < 0) return ret;

  ret = es7210_write_reg(i2c, addr, ES7210_PGA_GAIN2_REG, 0x00);
  if (ret < 0) return ret;

  /* Step 6: 禁用自动静音 */

  ret = es7210_write_reg(i2c, addr, ES7210_ADC_AUTOMUTE_REG, 0x00);
  if (ret < 0) return ret;

  /* Step 7: 取消 ADC 静音 */

  ret = es7210_write_reg(i2c, addr, ES7210_ADC12_MUTE_REG, 0x00);
  if (ret < 0) return ret;

  ret = es7210_write_reg(i2c, addr, ES7210_ADC34_MUTE_REG, 0xFF);
  if (ret < 0) return ret;

  /* Step 8: 上电 ADC1 和 ADC2 */

  ret = es7210_write_reg(i2c, addr, ES7210_POWERUP_REG, 0x30);
  if (ret < 0) return ret;

  audinfo("ES7210 configured: 16kHz, 16-bit, I2S slave, MIC1+MIC2\n");
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: esp32s3_es7210_initialize
 *
 * Description:
 *   Initialize ES7210 via I2C for microphone capture.
 *   This is NOT a NuttX Audio lower-half driver. It only configures the
 *   ES7210 chip registers. The actual audio device is registered by the
 *   subsequent board_i2sdev_initialize() call using the audio_i2s adapter.
 *
 * Input Parameters:
 *   i2c_port  - The I2C port (typically 0)
 *   i2c_addr  - The I2C 7-bit address (typically 0x40)
 *   i2c_freq  - The I2C bus frequency in Hz
 *
 * Returned Value:
 *   Zero on success, negated errno on failure.
 *
 ****************************************************************************/

int esp32s3_es7210_initialize(int i2c_port, uint8_t i2c_addr, int i2c_freq)
{
  FAR struct i2c_master_s *i2c;
  uint8_t chip_id;
  int ret;

  audinfo("ES7210 init: I2C%d addr=0x%02x freq=%d\n",
          i2c_port, i2c_addr, i2c_freq);

  /* 获取 I2C 总线实例 */

  i2c = esp32s3_i2cbus_initialize(i2c_port);
  if (i2c == NULL)
    {
      auderr("ERROR: Failed to get I2C%d bus\n", i2c_port);
      return -ENODEV;
    }

  /* 探测 ES7210 并读取芯片 ID */

  ret = es7210_read_reg(i2c, i2c_addr, ES7210_CHIP_ID_REG, &chip_id);
  if (ret < 0)
    {
      auderr("ERROR: ES7210 not found on I2C%d addr 0x%02x\n",
             i2c_port, i2c_addr);
      return -ENODEV;
    }

  audinfo("ES7210 chip ID: 0x%02x\n", chip_id);

  /* 配置 ES7210 */

  ret = es7210_configure(i2c, i2c_addr);
  if (ret < 0)
    {
      auderr("ERROR: ES7210 configure failed: %d\n", ret);
      return ret;
    }

  return OK;
}
```

### 5.5 esp32s3_es7210.h（新增文件）

```c
#ifndef __BOARDS_XTENSA_ESP32S3_ESP32S3_BOX_3_SRC_ESP32S3_ES7210_H
#define __BOARDS_XTENSA_ESP32S3_ESP32S3_BOX_3_SRC_ESP32S3_ES7210_H

/****************************************************************************
 * Name: esp32s3_es7210_initialize
 *
 * Description:
 *   Initialize ES7210 microphone ADC via I2C.
 *   Must be called before board_i2sdev_initialize().
 *
 ****************************************************************************/

#ifdef CONFIG_ESP32S3_BOX_AUDIO
int esp32s3_es7210_initialize(int i2c_port, uint8_t i2c_addr, int i2c_freq);
#endif

#endif
```

### 5.6 src/Make.defs（修改）

在 `esp32s3_board_touchsceen_gt911.c` 的条件编译块之后增加：

```makefile
ifeq ($(CONFIG_ESP32S3_BOX_AUDIO),y)
CSRCS += esp32s3_es7210.c
endif
```

### 5.7 defconfig（修改）

基于当前 `audio_event/defconfig`，需要做以下变更：

```diff
 # 去掉错误的 ES8311 配置（ES8311 仅用于播放，不用于录音）
-CONFIG_AUDIO_ES8311=y

 # 启用自定义 board
+CONFIG_ARCH_BOARD_CUSTOM=y
+CONFIG_ARCH_BOARD_CUSTOM_DIR="../vendor/espressif/boards/esp32s3/esp32s3-box-3"
+CONFIG_ARCH_BOARD_CUSTOM_DIR_RELPATH=y
+CONFIG_ARCH_BOARD_CUSTOM_NAME="esp32s3-box-3"

 # 启用 BOX-3 音频
+CONFIG_ESP32S3_BOX_AUDIO=y

 # 修正 I2S 引脚
 CONFIG_ESP32S3_I2S0_BCLKPIN=17
-CONFIG_ESP32S3_I2S0_DINPIN=38
+CONFIG_ESP32S3_I2S0_DINPIN=16
 CONFIG_ESP32S3_I2S0_MCLK=y
 CONFIG_ESP32S3_I2S0_MCLKPIN=2
-CONFIG_ESP32S3_I2S0_WSPIN=47
+CONFIG_ESP32S3_I2S0_WSPIN=45
+CONFIG_ESP32S3_I2S0_RX=y

 # 启用 I2C0（触摸屏已在用，音频共用）
+CONFIG_ESP32S3_I2C0=y
```

## 6. 双通道到单通道处理

ES7210 输出双通道数据（MIC1 + MIC2），audio_event 需要单声道 16kHz PCM16。
在 `audio_capture.c` 的 `audio_capture_read()` 返回数据后，应用层需要选择或
混合通道。

I2S 双通道数据布局（16-bit per sample）：

```text
buffer: [L0][R0][L1][R1][L2][R2] ...
         ↑MIC1  ↑MIC2
```

在 `audio_event_main.c` 的 `ring_append()` 中增加通道选择逻辑：

```c
/* 在 audio_capture_read() 返回后，提取单通道 */

static void extract_mono(const int16_t *stereo, int16_t *mono,
                         size_t stereo_samples)
{
  size_t i;
  size_t frames = stereo_samples / 2;

  for (i = 0; i < frames; i++)
    {
      /* 取 MIC1 (左通道)，或取两通道平均 */

      mono[i] = stereo[i * 2];
    }
}
```

## 7. 验收步骤

### 7.1 H0：编译和启动

```bash
# 构建
source build/envsetup.sh
./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/audio_event/ \
  -j8

# 烧录
cd nuttx && make -j8 flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BINDIR=./

# 启动后验证
nsh> help | grep audio_event
nsh> ls /dev/audio
```

验收：`/dev/audio/pcm_in0` 存在，`audio_event` 注册成功。

### 7.2 H1：麦克风录音验证

```bash
nsh> nxrecorder
# 录制 10 秒 PCM
```

在桌面检查：

- 文件长度正确（16000 * 2 * 10 = 320000 bytes）
- 非全零、非全满幅
- 无明显 DMA 断裂
- 安静时 RMS 在合理范围

### 7.3 H2：audio_event 文件推理

将 Sim 已验证的黄金 PCM 放入板端文件系统：

```bash
nsh> audio_event --file /data/golden/background.wav --once
```

### 7.4 H3：audio_event 实时推理

```bash
nsh> audio_event --device /dev/audio/pcm_in0
```

### 7.5 常见问题排查

| 现象 | 可能原因 | 排查方法 |
|---|---|---|
| `/dev/audio` 为空 | bringup 未执行到 I2S 初始化 | 检查 `dmesg` 中是否有 I2S 初始化日志 |
| 设备存在但读取全零 | ES7210 未配置或静音按钮按下 | 检查静音灯状态；用 I2C scan 确认 0x40 有应答 |
| 数据有值但幅值极小 | PGA 增益配置不当 | 调整 `PGA_GAIN1_REG` 和 `PGA_GAIN2_REG` |
| 采样率不对 | I2S MCLK/LRCK 分频配置错误 | 用示波器检查 MCLK、BCLK、LRCK 频率 |
| 只有一个通道有数据 | ADC 输入源配置错误 | 检查 `ADC1_SRC_REG` 和 `ADC2_SRC_REG` |
| 持续识别为 knock | 模型分布偏移（非驱动问题） | 参见模型校准文档 |

## 8. 后续演进

### 8.1 短期优化

- 实测 ES7210 寄存器值，根据示波器和录音结果调整
- 通道选择策略：MIC1 / MIC2 / 平均
- PGA 增益标定

### 8.2 可选升级到方案 B

如果后续需要运行时控制（调增益、切采样率、自动增益控制），可以将
`esp32s3_es7210.c` 扩展为完整的 NuttX Audio lower-half 驱动：

- 实现 `audio_ops_s` 全部回调
- 参考 `drivers/audio/es8311.c` 的模式
- 新增 Kconfig 选项 `CONFIG_AUDIO_ES7210`
- 预计工作量 ~1000 行

## 9. 参考文件

| 文件 | 用途 |
|---|---|
| `vendor/espressif/boards/esp32s3/esp32s3-eye/` | ARCH_BOARD_CUSTOM 实现参考 |
| `nuttx/boards/xtensa/esp32s3/esp32s3-eye/configs/openvela/defconfig` | CUSTOM board defconfig 参考 |
| `nuttx/boards/xtensa/esp32s3/common/src/esp32s3_es8311.c` | codec 初始化函数模式参考 |
| `nuttx/boards/xtensa/esp32s3/common/src/esp32s3_board_i2s.c` | `board_i2sdev_initialize()` 实现 |
| `nuttx/drivers/audio/audio_i2s.c` | audio_i2s 通用适配层 |
| `nuttx/drivers/audio/es8311.c` | 完整 codec lower-half 驱动参考 |
| `nuttx/boards/xtensa/esp32s3/esp32s3-box/` | 原始 NuttX board 代码（esp32s3-box-3 的复制基础） |
