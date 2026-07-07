# ESP32-S3-BOX-3 硬件初始化文档

## 1. 初始化总体顺序 (app_main)

```
app_main()
├── display_init()                         // 步骤1: 屏幕初始化
│   ├── bsp_display_start()
│   │   ├── lvgl_port_init()               // LVGL 端口子系统
│   │   ├── bsp_display_brightness_init()  // 背光 PWM (GPIO47, LEDC)
│   │   ├── bsp_display_lcd_init()         // LCD 初始化
│   │   │   ├── bsp_i2c_init()             // I2C 总线 (触摸屏/音频需要)
│   │   │   ├── spi_bus_initialize()       // SPI3 总线
│   │   │   ├── esp_lcd_new_panel_io_spi() // SPI 面板 IO
│   │   │   ├── esp_lcd_new_panel_st7789() // ST7789 或 ILI9341 控制器
│   │   │   ├── esp_lcd_panel_reset()      // 复位 LCD
│   │   │   ├── esp_lcd_panel_init()       // 初始化 LCD 控制器
│   │   │   └── lvgl_port_add_disp()       // 注册到 LVGL
│   │   └── bsp_display_indev_init()       // 触摸输入初始化
│   │       ├── bsp_touch_new()            // 自动探测 GT911/TT21100
│   │       └── lvgl_port_add_touch()      // 注册到 LVGL
│   ├── bsp_display_brightness_set(80)     // 背光亮度 80%
│   ├── bsp_display_lock(0)                // 获取 LVGL 互斥锁
│   ├── [创建 LVGL UI 控件]                 // 标题、进度条、数值标签
│   └── bsp_display_unlock()               // 释放 LVGL 互斥锁
│
├── bsp_audio_codec_microphone_init()      // 步骤2: 麦克风初始化
│   ├── bsp_audio_init(NULL)               // I2S 全双工音频
│   │   ├── i2s_new_channel() ×2           // TX + RX 通道 (I2S_NUM_1)
│   │   ├── i2s_channel_init_std_mode()    // 标准 Philips I2S 格式
│   │   └── i2s_channel_enable() ×2
│   ├── audio_codec_new_i2c_ctrl()         // I2C 控制接口 → ES7210
│   ├── es7210_codec_new()                 // ES7210 ADC 驱动实例
│   │   ├── 写入寄存器初始化序列
│   │   ├── 选择 MIC1 + MIC2 通道
│   │   └── 设置默认增益 30dB
│   └── esp_codec_dev_new()               // 封装为 codec 设备句柄
│
├── esp_codec_dev_set_in_gain(mic, 45dB)  // 步骤3: 设置输入增益
│
├── esp_codec_dev_open(mic, &cfg)         // 步骤4: 打开设备
│   ├── es7210_open()                     // 使能 ADC, 配置 MIC 偏置 (2.87V)
│   └── es7210_set_fs()                   // 设置采样率 16000 Hz
│
└── while(true)                           // 步骤5: 主循环
    ├── esp_codec_dev_read()              // 读取 512 采样点
    ├── 计算 avg_abs / peak / dBFS
    ├── display_update()                  // 更新 LVGL 界面
    └── ESP_LOGI()                        // 串口打印 (每 200ms)
```

**关键点：屏幕先初始化，麦克风后初始化，两者互相独立。**

---

## 2. GPIO 引脚分配

### 2.1 I2C 总线 (主)

| 信号 | GPIO | 说明 |
|------|------|------|
| SCL  | 18   | I2C 时钟 (I2C_NUM_1) |
| SDA  | 8    | I2C 数据 (I2C_NUM_1) |

此总线挂载设备：

| 设备 | I2C 地址 | 功能 |
|------|----------|------|
| ES8311   | 0x18 | 音频 DAC / 功放 |
| **ES7210** | **0x80** | **麦克风 ADC** |
| TT21100  | 0x24 | 触摸控制器 (主) |
| GT911    | 0x5D / 0x14 | 触摸控制器 (备) |
| ICM-42607-P | — | 6 轴 IMU |
| ATECC608A | — | 安全加密芯片 (通常未贴) |

### 2.2 I2C 总线 (Dock)

| 信号 | GPIO | 说明 |
|------|------|------|
| SCL  | 40   | Dock I2C 时钟 (I2C_NUM_0) |
| SDA  | 41   | Dock I2C 数据 (I2C_NUM_0) |

- 挂载设备: AHT30 温湿度传感器

### 2.3 I2S 音频总线

| 信号 | GPIO | 方向 | 说明 |
|------|------|------|------|
| MCLK | 2    | 输出 | 主时钟 |
| SCLK (BCLK) | 17 | 输出 | 位时钟 |
| LCLK (WS) | 45 | 输出 | 字选 / LRCLK |
| DOUT | 15   | 输出 | 播放数据 → ES8311 |
| **DSIN** | **16** | **输入** | **录音数据 ← ES7210** |

- I2S 外设: **I2S_NUM_1**, 全双工 (TX+RX)
- 模式: 标准 Philips I2S, 16-bit, 单声道默认
- ESP32-S3 为主机，ES7210 为从机

### 2.4 LCD 显示接口 (SPI3)

| 信号 | GPIO | 说明 |
|------|------|------|
| MOSI (DATA0) | 6 | SPI 数据 |
| SCLK (PCLK) | 7 | SPI 时钟 (40 MHz) |
| CS   | 5 | SPI 片选 |
| DC   | 4 | 数据/命令选择 |
| RST  | 48 | 复位 (与触摸共用) |
| 背光 | **47** | PWM 背光控制 (LEDC ch1, timer1, 5kHz, 10-bit) |

- 显示控制器: **ST7789** 或 **ILI9341** (自动检测，详见下文)
- 分辨率: **320×240**, RGB565
- SPI 外设: **SPI3_HOST**
- 像素时钟: 40 MHz
- 颜色空间: BGR (LVGL 配置 `LV_COLOR_16_SWAP` 需根据控制器设定)

**LCD 控制器自动检测逻辑**（位于 `esp-box-3.c`）：

```
1. 通过 I2C 探针检测触摸控制器:
   - 若检测到 TT21100 (0x24) → 面板为 ILI9341
   - 若检测到 GT911 (0x5D/0x14) → 面板为 ST7789
2. 调用对应的 esp_lcd_new_panel_*() 创建驱动
3. 写入厂商特定初始化序列 (vendor_specific_init)
```

不同控制器的关键差异：

| 参数 | ILI9341 | ST7789 |
|------|---------|--------|
| 颜色格式 | RGB565 (寄存器 0x3A = 0x55) | RGB565 (寄存器 0x3A = 0x55) |
| MADCTL (内存访问控制) | 0x36 = 0x08 (BGR) | 0x36 = 0x00 (RGB) |
| 像素顺序 | BGR | RGB |
| 初始化序列长度 | ~80 条寄存器命令 | ~40 条寄存器命令 |
| Gamma 校正曲线 | 独立正负 Gamma 表 | 内置默认 Gamma |
| VCOM 电压 | 需单独配置 | 内置 |

> **注意**: MADCTL 中的 BGR/RGB 位决定了显示屏从帧缓冲读取像素时的字节顺序。如果此位与实际面板不匹配，会导致红色和蓝色通道互换，表现为**画面整体偏蓝或偏红（偏色）**。

### 2.5 触摸屏

| 信号 | GPIO | 说明 |
|------|------|------|
| INT  | 3    | 触摸中断 |
| RST  | 48 (共用 LCD RST) | 复位 |

- 控制器: TT21100 或 GT911 (I2C 探针自动检测)

### 2.6 其他引脚

| 信号 | GPIO | 说明 |
|------|------|------|
| POWER_AMP_IO | 46 | ES8311 功放使能 |
| MUTE / BUTTON_MUTE | 1 | 静音按键 |
| BUTTON_CONFIG | 0 | 配置按键 (复位时控制启动模式) |
| USB D+ | 20 | USB OTG |
| USB D- | 19 | USB OTG |
| SD_POWER | 43 | SD 卡供电控制 |

### 2.7 SD 卡 (SDMMC)

| 信号 | GPIO |
|------|------|
| D0   | 9    |
| D1   | 13   |
| D2   | 42   |
| D3   | 12   |
| CMD  | 14   |
| CLK  | 11   |

---

## 3. 外设分配总览

| 外设 | 编号 | 用途 |
|------|------|------|
| I2C   | I2C_NUM_0 | Dock 总线 (AHT30 传感器) |
| I2C   | I2C_NUM_1 | 主总线 (ES8311, ES7210, 触摸, IMU) |
| I2S   | I2S_NUM_1 | 全双工音频 (ES7210 输入 + ES8311 输出) |
| SPI   | SPI3_HOST | LCD 显示屏 |
| LEDC  | Timer1, Ch1 | 背光 PWM |
| SDMMC | SLOT_0 | SD 卡 |

---

## 4. ES7210 麦克风配置详情

- **I2C 地址**: 0x80
- **模拟输入**: 4 通道 (MIC1–MIC4), 默认使用 MIC1 + MIC2
- **MIC 偏置电压**: 2.87V
- **增益挡位**: 0, 3, 6, ..., 30, 34.5, 36, 37.5 dB (寄存器 0x43–0x46)
- **应用配置**: 增益 45 dB (通过 esp_codec_dev API 设置到最近有效挡位)
- **采样参数**: 16000 Hz / 16-bit / 单声道 / MCLK=384×FS
- **I2S 模式**: 从机 (ESP32-S3 提供 MCLK/SCLK/WS)
- **数字音频输出引脚**: GPIO16 (DSIN)

---

## 5. SDK 关键配置 (sdkconfig)

| 配置项 | 值 |
|--------|-----|
| CONFIG_IDF_TARGET | esp32s3 |
| CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ | 160 |
| CONFIG_BSP_I2C_NUM | 1 |
| CONFIG_BSP_I2S_NUM | 1 |
| CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH | 1 |
| CONFIG_BSP_LCD_DRAW_BUF_HEIGHT | 100 |
| CONFIG_BSP_LCD_DRAW_BUF_DOUBLE | n (单缓冲) |
| CONFIG_CODEC_ES7210_SUPPORT | y |
| CONFIG_CODEC_ES8311_SUPPORT | y |
| CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS | 1 |
| CONFIG_LV_FONT_MONTSERRAT_14 | y |
| CONFIG_LV_FONT_MONTSERRAT_16 | y |
| CONFIG_LV_FONT_MONTSERRAT_20 | y |

---

## 6. 关键源文件

| 文件 | 说明 |
|------|------|
| `main/main.c` | 应用入口, 初始化和主循环 |
| `managed_components/espressif__esp-box-3/include/bsp/esp-box-3.h` | 所有 GPIO 宏定义 |
| `managed_components/espressif__esp-box-3/esp-box-3.c` | BSP 核心实现 (I2C/SPI/LCD/触摸/SD) |
| `managed_components/espressif__esp-box-3/esp-box-3_idf5.c` | I2S 音频初始化 |
| `managed_components/espressif__esp_codec_dev/device/es7210/es7210.c` | ES7210 驱动完整实现 |
| `managed_components/espressif__esp_codec_dev/device/es7210/es7210_reg.h` | ES7210 寄存器地址定义 |

---

## 7. LCD 显示控制器详细配置

### 7.1 SPI 时序参数

```
spi_device_interface_config_t:
  .clock_speed_hz = 40 * 1000 * 1000     // 40 MHz
  .mode           = 0                     // SPI mode 0 (CPOL=0, CPHA=0)
  .spics_io_num   = GPIO5                 // CS
  .queue_size     = 7                     // 7 条 SPI 事务
  .flags          = SPI_DEVICE_3WIRE      // 3 线 (MOSI 兼 MISO, 实际不读)
```

### 7.2 ILI9341 初始化序列 (vendor_specific_init)

关键寄存器命令（来自 `esp-box-3.c`）：

| 寄存器 | 值 | 说明 |
|--------|-----|------|
| 0x01 | — | 软件复位 (等待 150ms) |
| 0xCB | 0x39, 0x2C, 0x00, 0x34, 0x02 | 电源控制 A |
| 0xCF | 0x00, 0xC1, 0x30 | 电源控制 B |
| 0xE8 | 0x85, 0x00, 0x78 | 驱动时序控制 A |
| 0xEA | 0x00, 0x00 | 驱动时序控制 B |
| 0xED | 0x64, 0x03, 0x12, 0x81 | 电源时序控制 |
| 0xF7 | 0x20 | 泵比例控制 |
| 0xC0 | 0x23 | 电源控制 VRH[5:0] |
| 0xC1 | 0x10 | 电源控制 SAP[2:0]; BT[3:0] |
| 0xC5 | 0x3E, 0x28 | VCOM 控制 1 |
| 0xC7 | 0x86 | VCOM 控制 2 |
| **0x3A** | **0x55** | **像素格式: 16-bit RGB565 (65K 色)** |
| **0x36** | **0x08** | **MADCTL: BGR 像素顺序 (行地址自增, 列地址自增)** |
| 0xB1 | 0x00, 0x18 | 帧速率控制 (正常模式, ~79Hz) |
| 0xB6 | 0x08, 0x82, 0x27 | 显示功能控制 |
| 0xF2 | 0x00 | 3Gamma 功能禁用 |
| 0x26 | 0x01 | Gamma 曲线选择 (曲线 1) |
| 0xE0 | 0x0F,...,0x0F (15 bytes) | 正 Gamma 校正 |
| 0xE1 | 0x00,...,0x0F (15 bytes) | 负 Gamma 校正 |
| 0x11 | — | 退出睡眠模式 (等待 120ms) |
| 0x29 | — | 开启显示 |

### 7.3 LVGL 颜色空间配置

LVGL 端的关键配置（来自 `display.h`）：

```c
#define BSP_LCD_PIXEL_FORMAT        LV_COLOR_FORMAT_RGB565
#define BSP_LCD_COLOR_SPACE         LV_COLOR_SPACE_BGR   // ← 关键: BGR 顺序
#define BSP_LCD_H_RES               320
#define BSP_LCD_V_RES               240
#define BSP_LCD_SWAP_XY             false
#define BSP_LCD_MIRROR_X            false
#define BSP_LCD_MIRROR_Y            false
```

> **⚠️ 颜色空间匹配至关重要**: 如果 LCD 控制器 MADCTL 寄存器配置为 BGR 顺序 (0x36=0x08)，而 LVGL 端未做对应的字节交换，或者反过来，会导致每个像素的 R 和 B 分量互换，表现为**整体偏蓝或偏红的色调异常（偏色）**。

### 7.4 背光 PWM 配置

```c
ledc_timer_config_t:
  .speed_mode = LEDC_LOW_SPEED_MODE
  .timer_num  = LEDC_TIMER_1
  .duty_resolution = LEDC_TIMER_10_BIT   // 0–1023 级
  .freq_hz    = 5000                     // 5 kHz

ledc_channel_config_t:
  .channel    = LEDC_CHANNEL_1
  .gpio_num   = GPIO47                   // 背光控制引脚
```

---

## 8. ES7210 麦克风详细配置

### 8.1 初始化寄存器序列

ES7210 上电后必须写入完整的初始化序列（来自 `es7210.c`），否则 ADC 不会工作：

| 阶段 | 寄存器地址 | 写入值 | 说明 |
|------|-----------|--------|------|
| 复位 | 0x00 | 0x00 | 写任意值触发复位 |
| 时钟 | 0x06 | 0x00 | 启用主时钟 |
| 时钟 | 0x02 | 0x41 | 启用模拟 LDO |
| 时钟 | 0x03 | 0x40 | 模拟 LDO 选择 |
| 时钟 | 0x04 | 0x40 | 模拟时钟使能 |
| 上电 | 0x05 | 0x42 | 模拟上电 |
| 上电 | 0x01 | 0x0F | 数字上电 |
| 偏置 | 0x41 | 0x1F | MIC1/MIC2 偏置电压 (2.87V) |
| 偏置 | 0x42 | 0x1F | MIC3/MIC4 偏置电压 (2.87V) |
| 增益 | 0x43 | 0x30 | MIC1 增益 (30 dB) |
| 增益 | 0x44 | 0x30 | MIC2 增益 (30 dB) |
| 增益 | 0x45 | 0x30 | MIC3 增益 (30 dB) |
| 增益 | 0x46 | 0x30 | MIC4 增益 (30 dB) |
| 输入 | 0x40 | 0x03 | 选择 MIC1 + MIC2 |
| I2S | 0x11 | 0x22 | I2S 16-bit, 从机模式 |
| 模式 | 0x12 | 0x00 | 标准 I2S 格式 |
| MCLK | 0x20 | 0x04 | MCLK ratio=4 (256×FS) |
| ADC | 0x21 | 0x3F | 使能 ADC1–ADC4 |

### 8.2 I2S 时钟配置

```
MCLK = BCLK × 4 (MCLK ratio = 4)
BCLK = 采样率 × 通道数 × 位宽 × 2 (左右双声道) = 16000 × 1 × 16 × 2 = 512000 Hz
实际 BCLK 按立体声算 = 16000 × 2 × 16 = 512000 Hz
MCLK = 512000 × 256 / 16 ≈ 8192000 Hz (8.192 MHz)

es7210_set_fs() 根据采样率查表配置:
  - ADC 采样率寄存器 (0x07, 0x08, 0x09, 0x0A)
  - 数字滤波器系数 (0x2A–0x3F)
```

### 8.3 增益对应关系

ES7210 增益挡位与寄存器值对照表 (`gain_map[]`)：

| dB | 寄存器值 (bits [7:4]) |
|----|----------------------|
| 0.0  | 0x00 |
| 3.0  | 0x00 (bit 3 区分) → 实际 0x00/0x08 |
| 6.0  | 0x10 |
| 9.0  | 0x18 |
| 12.0 | 0x20 |
| 15.0 | 0x28 |
| 18.0 | 0x30 |
| 21.0 | 0x38 |
| 24.0 | 0x40 |
| 27.0 | 0x48 |
| 30.0 | 0x50 |
| 34.5 | 0x58 |
| 36.0 | 0x60 |
| 37.5 | 0x68 |

> **注意**: 应用层设置 45 dB 时，驱动会查找增益表中 ≥45 dB 的最小挡位，即 34.5 dB (5 挡位限制下实际只有 0–37.5 dB 范围，45 dB 会饱和到最近值)。

---

## 9. 已知问题与排查

### 9.1 屏幕显示彩色噪点 / 花屏

**现象**: 屏幕出现随机彩色条纹、雪花噪点、或完全花屏，而非正常的 UI 画面。

**根本原因分析**:

| 原因 | 详细说明 |
|------|---------|
| **固件不匹配 (最常见)** | 板子出厂预烧录 **NuttX** 系统。NuttX 的 bootloader、分区表和 NuttX 内核对 LCD 的初始化方式与 ESP-IDF 完全不同。如果只做 `idf.py monitor` 而没有先执行 `idf.py flash`，板子仍然运行 NuttX，而 NuttX 可能：1) 根本未初始化 LCD 控制器寄存器；2) 使用不同的 SPI 模式/时钟；3) 帧缓冲地址不匹配。这些都会导致 LCD 控制器从错误的内存位置读取数据并显示为随机噪点。 |
| **LCD 面板类型检测错误** | ILI9341 和 ST7789 的初始化序列长度和内容不同。如果自动检测逻辑因 I2C 总线未就绪而失败（例如触摸控制器无应答），可能选错驱动，导致 MADCTL、Gamma、VCOM 等寄存器配置错误，表现为画面颜色异常、条纹或噪点。 |
| **RGB/BGR 颜色空间不匹配** | ILI9341 的 MADCTL 寄存器设 BGR 顺序 (0x36 bit3=1)，而 LVGL 端期望 RGB 顺序时，每个像素的 R↔B 互换，导致**偏蓝或偏红的色调异常**（不算噪点，但属于颜色错误）。ST7789 默认 RGB 顺序。如果 LCD 控制器和 LVGL 的颜色空间配置不一致，就会出现此问题。 |
| **SPI 时钟过高或信号完整性问题** | 40 MHz SPI 时钟在飞线/面包板场景下可能导致数据错误。BOX-3 是 PCB 板载 LCD，正常情况下不应有此问题。 |
| **未正确复位 LCD** | LCD 上电后必须拉低 RST (GPIO48) ≥10ms 再拉高，随后等待 120ms。如果复位时序不对，LCD 控制器内部状态机未正确初始化，可能显示随机噪点。 |
| **帧缓冲未初始化** | LVGL 的 draw buffer 如果分配后未清零 (memset)，可能包含随机内存数据，第一帧会显示噪点直到 UI 控件刷新覆盖。 |

**排查步骤**:

1. **确认已烧录正确的 ESP-IDF 固件**（而非 NuttX）:
   ```bash
   idf.py flash monitor    # 先烧录再监视，不是仅 monitor
   ```
   正常启动日志应显示 `I (xxx) mic_test: Starting ESP32-S3-BOX-3 microphone test`，而非 `*** Booting NuttX ***`。

2. 检查启动日志中 LCD 初始化是否成功:
   ```
   I (xxx) BSP: Display init OK   ← 正常
   ```
   如果出现 `W (xxx) BSP: Display init failed`，说明 LCD 面板检测或 SPI 通信失败。

3. 确认 `sdkconfig` 中 `CONFIG_LV_COLOR_16_SWAP` 与 LCD MADCTL 的 BGR 位一致。

4. 用示波器检查 SPI 信号 (CS/DC/SCLK/MOSI) 是否有正常波形，RST 引脚复位后是否为高电平。

### 9.2 屏幕偏色 (红色/蓝色异常)

**现象**: UI 可以正常显示，但整体色调偏红或偏蓝。例如白色文字显示为偏蓝，或者红色和蓝色互换。

**根本原因**: **RGB ↔ BGR 颜色空间不匹配**。这是 9.1 中"噪点"问题的减轻版——LCD 控制器已正常初始化，但像素字节序错误。

具体机制：
- 每个像素 16 bit (RGB565): `RRRRR GGGGGG BBBBB`
- 如果 LCD 控制器 MADCTL 设 BGR 模式 (0x36 bit3=1)，控制器期望的数据顺序是 `BBBBB GGGGGG RRRRR`
- 但 LVGL 按标准 RGB565 写帧缓冲 → 红色和蓝色通道互换
- 视觉结果：原本红色 (#FF0000) 的像素变为蓝色 (#001F)，反之亦然

**排查**:

1. 确认当前使用的是哪款 LCD 面板（查看启动日志或 `bsp_display_lcd_init` 的返回值）。
2. 交叉验证 MADCTL 寄存器值和 LVGL `BSP_LCD_COLOR_SPACE` 宏：
   - ILI9341: MADCTL 0x36=0x08 (BGR) → LVGL 必须设 `LV_COLOR_SPACE_BGR`
   - ST7789: MADCTL 0x36=0x00 (RGB) → LVGL 必须设 `LV_COLOR_SPACE_RGB`
3. 若颜色仍不对，尝试交换 `LV_COLOR_16_SWAP` 的值。

### 9.3 麦克风无声音 / 采集全零

**现象**: `esp_codec_dev_read()` 成功返回 (ESP_OK)，但读取的数据全是 0 或只有微小噪声，`avg_abs` 始终在 0 附近，`dBFS` 显示 `-inf`。

**根本原因分析**:

| 原因 | 详细说明 |
|------|---------|
| **ES7210 未初始化 (最常见)** | ES7210 ADC 是一个复杂的可编程器件，上电后默认处于掉电状态。**必须通过 I2C 写入完整的寄存器初始化序列**（见第 8 节），才能：1) 使能内部 LDO 和时钟；2) 上电模拟/数字电路；3) 配置 MIC 偏置电压；4) 设置增益；5) 选择输入通道；6) 配置 I2S 格式和主从模式；7) 使能 ADC 通道。NuttX 系统不包含 ES7210 驱动，不会执行任何初始化，因此麦克风完全不工作。 |
| **I2S 时钟未配置** | ES7210 在从机模式下，所有 I2S 时钟 (MCLK/SCLK/WS) 由 ESP32-S3 提供。如果 `bsp_audio_init()` 未执行或 I2S 配置错误，ES7210 没有时钟源，ADC 不会开始采样。即使 ADC 已正确初始化，没有 MCLK 意味着 delta-sigma 调制器无法工作。 |
| **MIC 偏置电压未开启** | ECM (驻极体) 麦克风和 MEMS 模拟麦克风需要直流偏置电压才能工作。ES7210 通过寄存器 0x41/0x42 提供可编程 MICBIAS。如果偏置未开启，麦克风无输出信号。 |
| **输入通道未选择** | 寄存器 0x40 控制输入通道选择。默认值可能为 0x00（无通道选中），必须写入 0x03 选择 MIC1+MIC2。 |
| **增益设置过低** | 默认增益 30 dB (0x30)。在安静环境中，30 dB 增益下 ADC 输出可能仅有个位数 LSB 的底噪，看起来像"无声音"。应用层额外调用 `esp_codec_dev_set_in_gain(45)` 提高增益。 |
| **I2C 通信失败** | ES7210 的 I2C 地址为 0x80 (7-bit 地址 0x40 << 1)。如果 I2C 总线未初始化、SCL/SDA 引脚配置错误、或总线被其他设备拉死，寄存器写入全部静默失败，ES7210 保持掉电状态。检查 I2C 地址扫描是否能看到 0x40。 |
| **NuttX 固件残留** | 与屏幕问题同理——NuttX 没有 ES7210 驱动，不配置 I2S，不初始化 I2C 音频设备。麦克风根本无法工作。 |

**排查步骤**:

1. **确认已烧录正确的固件**:
   ```bash
   idf.py flash monitor
   ```
   启动日志必须显示 `Starting ESP32-S3-BOX-3 microphone test`，而不是 `Booting NuttX`。

2. 检查启动日志中音频初始化是否成功:
   ```
   I (xxx) ES7210: ES7210 initialized successfully   ← 正常
   ```
   如果出现 `E (xxx) ES7210: ...` 错误，说明 I2C 通信或寄存器配置失败。

3. 用 I2C 扫描确认 ES7210 可见:
   - ES7210 I2C 地址: 0x80 (8-bit 写地址) / 0x40 (7-bit 地址)
   - 确认 I2C_NUM_1 的 SCL(GPIO18)、SDA(GPIO8) 未被其他外设占用

4. 用示波器/逻辑分析仪检查:
   - I2S MCLK (GPIO2): 应有 ~8.2 MHz 连续时钟
   - I2S SCLK (GPIO17): 应有 512 kHz 位时钟
   - I2S WS (GPIO45): 应有 16 kHz 帧同步信号
   - I2S DSIN (GPIO16): 应有数字音频数据流
   - 如果前三路有时钟但 DSIN 无数据 → ES7210 未正确初始化
   - 如果 MCLK 都没有 → I2S 初始化失败

5. 临时测试：用手触摸/轻敲麦克风通孔，观察 `avg_abs` 是否变化。正常时应明显上升。

