# ESP32-S3-BOX-3介绍
## 简介
ESP32-S3-BOX-3 主控单元由 ESP32-S3-WROOM-1 模组驱动，该模块提供了 2.4 GHz Wi-Fi + Bluetooth 5 (LE) 的无线功能，同时具备 AI 加速能力。在 ESP32-S3 SoC 提供的 512 KB SRAM 基础上，该模块还附带额外的 16 MB 四线 Flash 和 16 MB 八线 PSRAM。开发板配备一片 2.4 英寸 320 x 240 的 SPI 接口触摸屏（‘红圈’支持触摸），两个数字麦克风，一个扬声器，三轴陀螺仪，三轴加速度计，一个用于电源和下载/调试的 Type-C 接口，一个可实现硬件扩展的高密度 PCIe 连接器，以及三个功能按钮。


## 具体参数
为您将具体参数汇总为以下表格，方便对照查看：

| 类别 | 具体参数 |
| :--- | :--- |
| **微控制器** | **类型**：ESP32-S3<br>**CPU**：双核 Xtensa® 32bit LX7，最高 240 MHz |
| **存储器** | **SRAM**：512 KB<br>**ROM**：384 KB<br>**PSRAM**：Octal SPI，16 MB（运行速度 120 MHz）<br>**外部 Flash**：Quad SPI，16 MB |
| **AI 特性** | **AI 算法**：神经网络、声学算法等<br>**计算加速**：向量运算、复数运算、FFT 等 |
| **无线通讯** | **Wi-Fi**：2.4 GHz，IEEE 802.11b/g/n<br>**蓝牙**：Bluetooth® 5 LE & Bluetooth® mesh |
| **显示** | **类型**：2.4 英寸 LCD<br>**分辨率**：240 × 320 像素<br>**接口**：SPI（速率 40 MHz）<br>**触控**：电容式，支持 10 点触控<br>**驱动 IC**：ILI9342C |
| **音频输入** | **麦克风**：双麦克风（Dual Mic）<br>**ADC 芯片**：ES7210<br>**静音**：支持 |
| **音频输出** | **喇叭**：8 Ohm 1 W<br>**功放 (PA) 芯片**：NS4150<br>**编解码 (Codec) 芯片**：ES8311 |
| **传感器** | **类型**：3 轴陀螺仪、3 轴加速度计<br>**型号**：ICM-42607-P |
| **接口** | **USB Type-C**：用于供电、USB 下载、JTAG 调试及通用 USB 设备功能<br>**金手指 (Goldfinger)**：用于 I/O 扩展 |
| **用户交互** | **板载按键**：Reset、Boot、Mute<br>**板载 LED 灯**：电源指示灯、静音指示灯 |
| **OS / SDK** | **原生 OS**：FreeRTOS<br>**开发 SDK**：ESP-IDF |
| **外观尺寸** | **尺寸**：61 × 66 × 16.6 mm<br>**重量**：292 g |
| **电源** | **USB-C 输入**：5 V - 2.0 A<br>**电池**：无（N/A） |

# 麦克风与屏幕（LCD）引脚分析

基于ESP32-S3-BOX-3-MB原理图，以下是对麦克风和屏幕相关引脚的详细分析。

---

## 一、LCD 屏幕引脚

LCD 通过 SPI 接口与 ESP32-S3-WROOM-1 模块通信，控制信号引脚分配如下：

| 信号名称 | 对应GPIO | 功能说明 |
|---|---|---|
| **LCD_DC** | GPIO4 | 数据/命令选择 |
| **LCD_CS** | GPIO5 | SPI片选 |
| **LCD_SDA** | GPIO6 | SPI数据线（MOSI） |
| **LCD_SCK** | GPIO7 | SPI时钟 |
| **LCD_RST** | GPIO48 | 屏幕复位（通过 GPIO48_L 网络，经 U18A LBSS138DW1T1G MOSFET 驱动） |
| **LCD_CTRL** | GPIO47 | 背光控制使能 |

**LCD 背光电路**：由 U15 ETA1611 LED驱动芯片控制，输入来自 VCC_5V，经 L6 (10uH) 电感和 D10 LMBR545ET1G 肖特基二极管，通过 R126 (12Ω) 电阻连接 LED_K，LCD_CTRL 信号经 R137 连接到 ETA1611 的 EN 使能引脚。

**LCD 连接器**（FPC 24pin）信号映射：
- LEDA → LED背光正极（经 R77 2Ω电阻）
- LEDK → LED背光负极
- TFT_SCL → LCD_SCK
- TFT_SDA → LCD_SDA
- DCX → LCD_DC
- TFT_RST → LCD_RST
- LCD_CS → SPI片选

---

## 二、麦克风（AMIC）引脚

麦克风为 **2路模拟麦克风（AMIC x2）**，通过 FPC 8pin 连接器接入，信号经 ES7210 ADC 芯片采集后以 I2S 数字格式传输给 ESP32-S3。

### 2.1 ES7210 ADC 芯片 I2S 接口引脚

| 信号名称 | 对应GPIO | 功能说明 |
|---|---|---|
| **I2S_MCLK** | GPIO2 | I2S主时钟 |
| **I2S_SCLK** | GPIO17 | I2S串行时钟 |
| **I2S_LRCK** | GPIO45 | I2S帧时钟（左右声道选择） |
| **I2S_ADC_SDOUT** | GPIO16 | I2S数据输出（经 U8 SN74LVC1G125 缓冲后） |

### 2.2 ES7210 ADC 芯片 I2C 配置接口

ES7210 的 I2C 地址（AD1=0, AD0=0）为 **0x80 (8bit) / 0x40 (7bit)**，默认地址。I2C 总线与 LCD、触摸屏、IMU 等共用：
- **I2C_SCL** → GPIO18
- **I2C_SDA** → GPIO8

### 2.3 模拟麦克风信号连接

**FPC 8pin 连接器**信号分配：

| 连接器引脚 | 信号名称 | 说明 |
|---|---|---|
| 1 | ADC_MIC1P | 麦克风1正极输入 |
| 2 | ADC_MIC1N | 麦克风1负极输入 |
| 3 | ADC_MIC2P | 麦克风2正极输入 |
| 4 | ADC_MIC2N | 麦克风2负极输入 |
| 5 | ADC_GND | 模拟地 |
| 6 | ADC_MICBIAS12 | 麦克风偏置电压 |
| 7 | ADC_GND | 模拟地 |
| 8 | ADC_GND | 模拟地 |

麦克风差分信号通过 **1uF/10V 隔直电容**（C37、C38、C42、C45）连接至 ES7210 的 MIC1P/MIC1N 和 MIC2P/MIC2N 引脚。ES7210 的 MICBIAS 引脚通过电阻网络为麦克风提供偏置电流。

### 2.4 麦克风静音控制

原理图中包含 **ADC MUTE** 电路（Power for ADC MUTE / Data for ADC MUTE）：
- **MUTE_PWR** 信号：控制 Q1 AO3401A PMOS 管，实现 ES7210 电源的通断（通过 R21 10K 电阻）
- **MUTE_DATA** 信号：通过 U8 SN74LVC1G125 缓冲器控制 I2S 数据输出的使能/禁止
- **MUTE_STATUS_L** → GPIO1：静音状态反馈信号

---

## 三、总结

整个音频输入链路为：**模拟麦克风 → FPC连接器 → 隔直电容 → ES7210 ADC（I2S接口）→ ESP32-S3**；屏幕控制链路为：**ESP32-S3 → SPI接口（GPIO4-7）+ 背光控制（GPIO47）→ LCD FPC连接器**。两者共用 I2C 总线（GPIO18 SCL / GPIO8 SDA）进行初始化配置。

