# 基于 openvela 的本地音频事件检测系统-ccf_audioevent

`ccf_audioevent` 是一个面向 openvela 的本地音频事件检测作品目录。当前主路径是
`ESP32-S3-N16R8 DevKit + INMP441 数字麦克风 + 0.96 寸 I2C OLED`，核心应用是
`audio_event`：采集音频、提取 log-mel/delta 特征、运行 TFLite Micro 模型，并在
OLED 上显示检测状态。

`audio_record` 和 `audio_test` 是辅助工具：前者用于导出实录 WAV，后者用于检查
I2S/INMP441 采集质量。goldfish 模拟器仍保留，用于文件输入、模型加载和大屏 LVGL UI
验证，但放在真机主流程之后。

按用途整理的文档入口见 [`docs/README.md`](docs/README.md)。

## 初赛要求对应关系

| 初赛要求 | 当前实现 | 对应材料 |
| --- | --- | --- |
| 离线本地闭环 | `audio_event` 在端侧完成采集、特征提取、TFLite Micro 推理和 OLED/串口输出 | 本文“快速开始”“audio_event 主应用” |
| 至少 2 类音频事件 | 当前支持 `knock`、`cough`，并保留 `background`、`silence` 作为背景/静音类别 | [docs/项目基线/事件定义与触发口径说明.md](docs/项目基线/事件定义与触发口径说明.md) |
| 可复现运行 | 提供仓库拉取、openvela 软链接、ESP32-S3 DevKit 构建、烧录和运行命令 | 本文“准备工作”“快速开始”“真机构建和烧录” |
| 基本异常处理 | 覆盖音频设备打开失败、采集全 0、OLED 不可用、模型加载失败等场景 | [docs/使用与调试/异常处理.md](docs/使用与调试/异常处理.md) |
| 运行演示 | 提供真机运行日志和演示视频 | [logs/演示日志.md](logs/演示日志.md)、[logs/演示视频.mp4](logs/演示视频.mp4) |
| 延迟与误报/漏报数据 | 提供端侧延迟、训练集/测试集指标和已知限制 | [docs/项目基线/性能与评估.md](docs/项目基线/性能与评估.md) |
| 开源协议与第三方声明 | 补充项目协议、NOTICE 和第三方依赖/数据集声明 | [LICENSE](LICENSE)、[NOTICE](NOTICE)、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) |

## 初赛任务
1. 离线本地闭环：完成采集 → 预处理/特征提取 → 模型识别 → 输出/告警（LED/蜂鸣器/屏幕/串口等）全流程在端侧完成；结果可验证、可重复运行。
2. 基础类别覆盖：至少支持 2 类音频事件（如玻璃破碎、咳嗽），提供事件定义与触发口径说明。
3. 可复现与工程化：提供清晰的运行/部署说明，一键运行方式，具备基本异常处理。
4. 演示参考logs/演示视频.mp4和logs/演示日志.md

## 目录结构

```text
ccf_audio/
├── app/audio_event/                  # 主应用：音频事件检测
│   ├── model/                         # TFLite Micro 模型与元信息
│   ├── ui/                            # 320x240 LVGL dashboard，模拟器使用
│   └── ui_oled/                       # 128x64 OLED compact UI，真机使用
├── app/audio_record/                 # 辅助工具：录音并导出 WAV base64
├── app/audio_test/                   # 辅助工具：PCM/INMP441 采集诊断
├── board/esp32s3-devkit/             # 当前真机主 board
├── board/goldfish-arm64/configs/
│   └── audio_event/                  # goldfish 模拟器配置源文件
├── archive/esp32s3-box-3/            # ESP32-S3-BOX-3 历史适配归档
├── docs/                             # 赛题、异常处理、性能评估和事件口径文档
├── train/                            # 模型训练脚本与训练产物
│   ├── train_audio_event_model.py     # 小型模型训练脚本（当前部署模型）
│   ├── train_large_model.py           # 中/大型模型训练脚本（复赛模型优化备用）
│   ├── small_clean/                   # small 模型训练产物（含 metrics/模型文件）
│   ├── medium_clean/                  # medium 模型训练产物
│   └── large_clean/                   # large 模型训练产物
├── scripts/                          # 字体等辅助脚本
├── LICENSE                           # 项目源码开源协议
├── NOTICE                            # 项目和数据来源声明
├── THIRD_PARTY_NOTICES.md            # 第三方依赖和数据集许可摘要
└── readme.md
```

## 准备工作

### 1. 准备 openvela 工作区

先准备可构建的 openvela 工作区，并确认可以在 openvela 根目录执行 `./build.sh`。
下文默认从 openvela 的上一级目录进入工作区：

```bash
cd openvela
OPENVELA_ROOT="$(pwd)"
```

### 2. 拉取 ccf_audio 仓库

推荐将本仓库放在 openvela 根目录下。Git 拉取后的默认目录名是 `ccf_audio`：

```bash
cd openvela
git clone https://gitlink.org.cn/yang1234/ccf_audio.git
```

如果本地已经存在该目录，进入后更新即可：

```bash
cd openvela/ccf_audio
git pull --ff-only
```

### 3. 建立工作区软链接

`ccf_audio` 作为独立作品仓维护，openvela 构建系统需要通过软链接找到 example app、
board 和模拟器配置。在 openvela 根目录执行：

```bash
cd openvela
OPENVELA_ROOT="$(pwd)"
CCF_AUDIO_ROOT="$OPENVELA_ROOT/ccf_audio"

ln -sfnT "$CCF_AUDIO_ROOT/app/audio_event" \
  "$OPENVELA_ROOT/apps/examples/audio_event"

ln -sfnT "$CCF_AUDIO_ROOT/app/audio_record" \
  "$OPENVELA_ROOT/apps/examples/audio_record"

ln -sfnT "$CCF_AUDIO_ROOT/app/audio_test" \
  "$OPENVELA_ROOT/apps/examples/audio_test"

ln -sfnT "$CCF_AUDIO_ROOT/board/esp32s3-devkit" \
  "$OPENVELA_ROOT/vendor/espressif/boards/esp32s3/esp32s3-devkit"

mkdir -p "$OPENVELA_ROOT/vendor/openvela/boards/vela/configs/goldfish-audio_event"
rm -f "$OPENVELA_ROOT/vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig"
ln -sfn "$CCF_AUDIO_ROOT/board/goldfish-arm64/configs/audio_event/defconfig" \
  "$OPENVELA_ROOT/vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig"
```

检查：

```bash
readlink -f apps/examples/audio_event
readlink -f apps/examples/audio_record
readlink -f apps/examples/audio_test
readlink -f vendor/espressif/boards/esp32s3/esp32s3-devkit
readlink -f vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig
```

`goldfish-audio_event` 目录本身必须是
`vendor/openvela/boards/vela/configs/` 下的真实目录，只软链接其中的 `defconfig`。
不要把整个 `goldfish-audio_event` 目录软链接到 `ccf_audio`，否则非 CMake 构建会
报 `File Make.defs could not be found`。

`audio_record` 和 `audio_test` 是新增 example。软链接存在后重新 configure 或构建，
`apps/examples/Kconfig` 生成阶段会自动加入：

```text
source "<openvela-root>/apps/examples/audio_record/Kconfig"
source "<openvela-root>/apps/examples/audio_test/Kconfig"
```

如果 NSH 中没有 `audio_record` 或 `audio_test`，优先检查软链接和重新 configure 状态。

## 快速开始

完成准备工作后，构建 ESP32-S3 DevKit 真机固件：

```bash
cd openvela
OPENVELA_ROOT="$(pwd)"
CCF_AUDIO_ROOT="$OPENVELA_ROOT/ccf_audio"

bash "$CCF_AUDIO_ROOT/scripts/fix_box3_mbedtls_header_priority.sh"

./build.sh vendor/espressif/boards/esp32s3/esp32s3-devkit/configs/audio_event/ -j8 &
BUILD_PID=$!

bash "$CCF_AUDIO_ROOT/scripts/fix_box3_mbedtls_disable_ccm.sh" &
CCM_FIX_PID=$!
bash "$CCF_AUDIO_ROOT/scripts/fix_box3_spinlock_initializer.sh" &
SPINLOCK_FIX_PID=$!

wait "$BUILD_PID"
BUILD_STATUS=$?
wait "$CCM_FIX_PID" "$SPINLOCK_FIX_PID"
test "$BUILD_STATUS" -eq 0
```

说明：ESP32-S3 首次构建或 `distclean` 后，`esp-hal-3rdparty` 会在构建过程中生成。
Fix 1 先修正头文件优先级，Fix 2 和 Fix 3 与构建同步运行并等待目标文件出现。

烧录并打开串口：

```bash
cd nuttx && make flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BAUD=921600 && cd ..
picocom -b 115200 /dev/ttyACM0
```

进入 NSH 后运行主应用：

```text
nsh> audio_event --device /dev/audio/pcm_in1 --audio-stats
```

`audio_test`、`audio_record` 和 goldfish 模拟器属于辅助验证路径，README 只保留主流程；
采集诊断、录音导出和模拟器构建运行见“辅助工具和模拟器”。

## 基础指标摘要

当前部署模型为 small int8 模型，输入为 16 kHz、1 秒、mono int16，特征形状为
`49 x 40 x 3`，类别为 `knock, cough, background, silence`。

| 指标 | 数值 |
| --- | --- |
| 模型大小 | 11,984 bytes |
| 参数量 | 2,148 |
| Tensor arena 配置 / 使用 | 65,536 bytes / 22,708 bytes |
| 离线测试样本数 | 570 |
| 离线 Accuracy | 92.98% |
| 离线 False alarm rate | 4.57% |
| 离线 Miss rate | 2.27% |
| 端侧单窗口处理耗时 | 约 420 - 430 ms |

详细评估口径、混淆矩阵、阈值说明和端侧延迟说明见
[`docs/项目基线/性能与评估.md`](docs/项目基线/性能与评估.md)。

## 开源协议与合规摘要

项目源码使用 MIT License，见 [`LICENSE`](LICENSE)。第三方依赖和数据来源声明见
[`NOTICE`](NOTICE) 与 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。

当前模型训练数据来源包括 FSD50K、ESC-50 以及自采音频。FSD50K 含混合 Creative
Commons 许可音频片段，ESC-50 整体为 Creative Commons Attribution-NonCommercial
3.0；商业使用前需要单独审查数据来源和模型派生物合规性。

## 真机硬件

当前真机构建使用 `esp32s3-devkit`：

```text
vendor/espressif/boards/esp32s3/esp32s3-devkit/configs/audio_event/
```

### INMP441 接线

| INMP441 引脚 | ESP32-S3 DevKit | 说明 / 配置 |
| --- | --- | --- |
| VDD | 3V3 | 使用 3.3 V，不要接 5 V |
| GND | GND | 共地 |
| SCK / BCLK | GPIO18 | `CONFIG_ESP32S3_I2S1_BCLKPIN=18` |
| WS / LRCK | GPIO17 | `CONFIG_ESP32S3_I2S1_WSPIN=17` |
| SD / DOUT | GPIO15 | `CONFIG_ESP32S3_I2S1_DINPIN=15` |
| L/R | GND | 选择 Left slot，即 slot 0 |

INMP441 不需要 MCLK。当前 I2S1 采集为 `16 kHz, 2ch, 32-bit`，应用侧选择 slot 0，
再右移 16 位转换为模型需要的 `16 kHz, mono, int16`。

若把 `L/R` 接到 3V3，应将应用侧 slot 改为 1 后重新构建。

### 0.96 寸 OLED 接线

| OLED 引脚 | ESP32-S3 DevKit | 说明 / 配置 |
| --- | --- | --- |
| VCC | 3V3 | 使用 3.3 V |
| GND | GND | 共地 |
| SCL | GPIO5 | `CONFIG_ESP32S3_I2C0_SCLPIN=5` |
| SDA | GPIO4 | `CONFIG_ESP32S3_I2C0_SDAPIN=4` |
| I2C 地址 | `0x3C` | `CONFIG_SSD1306_I2CADDR=60` |

启动后 board bring-up 会初始化 I2C0 和 OLED。若屏幕亮但内容镜像、错位或缺字，先确认
模块到底是 SSD1306 还是 SH1106 兼容屏；SH1106 常见 132 列内部显存，需要单独适配。

## 真机构建和烧录

构建：

```bash
cd openvela
OPENVELA_ROOT="$(pwd)"
CCF_AUDIO_ROOT="$OPENVELA_ROOT/ccf_audio"

bash "$CCF_AUDIO_ROOT/scripts/fix_box3_mbedtls_header_priority.sh"

./build.sh vendor/espressif/boards/esp32s3/esp32s3-devkit/configs/audio_event/ -j8 &
BUILD_PID=$!

bash "$CCF_AUDIO_ROOT/scripts/fix_box3_mbedtls_disable_ccm.sh" &
CCM_FIX_PID=$!
bash "$CCF_AUDIO_ROOT/scripts/fix_box3_spinlock_initializer.sh" &
SPINLOCK_FIX_PID=$!

wait "$BUILD_PID"
BUILD_STATUS=$?
wait "$CCM_FIX_PID" "$SPINLOCK_FIX_PID"
test "$BUILD_STATUS" -eq 0
```

注意在 `distclean` 后的首次构建时，Fix 2 和 Fix 3 需要与构建**并行运行**。
因为 `esp-hal-3rdparty` 的 git clone 和 patch 是构建过程中异步执行的，
这两个脚本会等待目标文件出现（最多 180 秒）。

烧录并打开串口：

```bash
cd nuttx && make flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BAUD=921600 && cd ..
picocom -b 115200 /dev/ttyACM0
```

开发板如果有两个 Type-C 口，通常一个是 USB/JTAG/COM，另一个是 USB OTG。串口监视使用
COM 口；烧录后若没有马上进 NSH，按一下 RESET。

进入 NSH 后先确认 app 注册：

```text
nsh> help
```

期望 Builtin Apps 中包含：

```text
audio_event     audio_record    audio_test
```

## audio_event 主应用

`audio_event` 是主流程。它默认面向 16 kHz、1 秒、mono int16 输入，当前模型使用
`49 x 40 x 3` 的 log-mel + delta 特征，类别顺序为：

```text
knock, cough, background, silence
```

常用启动命令：

```text
nsh> audio_event --device /dev/audio/pcm_in1 --audio-stats
```

诊断时建议加上 profile：

```text
nsh> audio_event --device /dev/audio/pcm_in1 --audio-stats --profile
```

如果怀疑 OLED/I2C 影响音频采集，可临时关闭 OLED：

```text
nsh> audio_event --device /dev/audio/pcm_in1 --audio-stats --profile --no-oled
```

只验证模型能否加载和推理：

```text
nsh> audio_event --model-smoke
```

只跑一轮设备采集：

```text
nsh> audio_event --device /dev/audio/pcm_in1 --audio-stats --once
```

### 运行日志判断

真机 INMP441 路径启动后应看到类似日志：

```text
[audio] configure input pcm rate=16000 channels=2 bits=32
[audio] INMP441 adapter: slot=0 shift=16 output=mono int16
[audio] capturing /dev/audio/pcm_in1 at 16000 Hz, device=2ch/32-bit, app=mono/16-bit
[app] capture worker: ring=32000 samples window=16000 hop=4000
```

含义：

- `device=2ch/32-bit`：I2S 驱动按 INMP441 的 32-bit slot 采集。
- `app=mono/16-bit`：应用已转换成模型输入格式。
- `ring=32000`：采集线程维护 2 秒环形缓冲区。
- `window=16000`：每次推理取最新 1 秒音频。
- `hop=4000`：默认每 250 ms 尝试一次新窗口。

`--audio-stats` 会打印每个 1 秒窗口的统计：

```text
[audio_stats] t=1024 ms wall=1050 ms min=-4439 max=28605 mean=3627 rms=8558 zero=0/16000
[infer] t=1024 ms wall=1050 ms class=background probs_permille=[39 23 934 4]
```

字段说明：

- `t`：音频流时间，按已采集样本数换算。
- `wall`：真实墙钟时间。
- `min/max`：该窗口内 PCM16 最小/最大样本值，出现负数是正常的。
- `mean`：直流偏置，理想情况下应接近 0。
- `rms`：音量/能量，敲门、咳嗽时会明显升高。
- `zero`：值为 0 的样本数量，过高可能是静音段、slot 不对或缓冲异常。
- `probs_permille`：四类概率的千分比，顺序是 `[knock cough background silence]`。

当前真机阈值建议：

```text
knock >= 380 permille
cough >= 750 permille
consecutive hits = 2
```

敲门实测常见概率不如咳嗽尖锐，因此敲门阈值低于咳嗽阈值。若误报偏多，先把
`CONFIG_EXAMPLES_AUDIO_EVENT_KNOCK_THRESHOLD` 提到 `420` 左右再测试。

### OLED 显示

DevKit 真机启用 `CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI`，不是把 320x240 LVGL 页面缩小，
而是独立的 128x64 compact UI。

监听时显示当前状态、置信度和音量：

```text
AUDIO EVENT
BGND / QUIET / KNOCK / COUGH
CONF xx%
RMS xxxx
[volume bar]
```

触发时显示目标事件：

```text
KNOCK!
CONF 762
HIT 2/2
```

冷却期间显示：

```text
COOLDOWN
1.2s
LAST KNOCK
```

## 辅助工具和模拟器

README 以 `audio_event` 真机闭环为主。辅助工具和模拟器说明拆分到 docs：

- [`docs/使用与调试/audio_test采集诊断.md`](docs/使用与调试/audio_test采集诊断.md)：检查 INMP441 接线、I2S slot、位宽转换和削波。
- [`docs/使用与调试/audio_record录音导出.md`](docs/使用与调试/audio_record录音导出.md)：录制真机 WAV，通过串口 base64 导出到电脑。
- [`docs/使用与调试/goldfish模拟器.md`](docs/使用与调试/goldfish模拟器.md)：goldfish-arm64 构建、运行和 320x240 LVGL dashboard 验证。

## 关键配置

`board/esp32s3-devkit/configs/audio_event/defconfig` 重点配置：

- `CONFIG_EXAMPLES_AUDIO_EVENT=y`
- `CONFIG_EXAMPLES_AUDIO_EVENT_DEVPATH="/dev/audio/pcm_in1"`
- `CONFIG_EXAMPLES_AUDIO_EVENT_INMP441_32BIT=y`
- `CONFIG_EXAMPLES_AUDIO_EVENT_KNOCK_THRESHOLD=380`
- `CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI=y`
- `CONFIG_EXAMPLES_AUDIO_RECORD=y`
- `CONFIG_EXAMPLES_AUDIO_TEST=y`
- `CONFIG_EXAMPLES_AUDIO_TEST_CHANNELS=2`
- `CONFIG_ESP32S3_DEVKIT_INMP441=y`
- `CONFIG_ESP32S3_DEVKIT_OLED=y`
- `CONFIG_ESP32S3_I2C0_SCLPIN=5`
- `CONFIG_ESP32S3_I2C0_SDAPIN=4`
- `CONFIG_ESP32S3_I2S1_BCLKPIN=18`
- `CONFIG_ESP32S3_I2S1_WSPIN=17`
- `CONFIG_ESP32S3_I2S1_DINPIN=15`
- `CONFIG_ESP32S3_I2S1_DATA_BIT_WIDTH_32BIT=y`
- `CONFIG_LCD_SSD1306_I2C=y`
- `CONFIG_LCD_UG2864HSWEG01=y`
- `CONFIG_TFLITEMICRO=y`
- `CONFIG_MATH_KISSFFT=y`
- `CONFIG_LIBCXX=y`

`audio_record` 和 `audio_test` 的 INMP441 适配配置由 Kconfig 在
`CONFIG_ESP32S3_DEVKIT_INMP441=y` 时默认启用。默认 slot 为 0，shift 为 16。

`board/goldfish-arm64/configs/audio_event/defconfig` 重点配置：

- `CONFIG_EXAMPLES_AUDIO_EVENT=y`
- `CONFIG_EXAMPLES_AUDIO_EVENT_DEVPATH="/dev/audio/pcm0c"`
- `CONFIG_EXAMPLES_AUDIO_EVENT_UI=y`
- `CONFIG_TFLITEMICRO=y`
- `CONFIG_MATH_KISSFFT=y`
- `CONFIG_SYSTEM_FLATBUFFERS=y`

goldfish 配置已尽量移除与 `audio_event` 无关的重型组件，例如 Android Binder、
QuickApp、Feature Framework、Media server 和 curl。

## ESP32-S3-BOX-3 归档

ESP32-S3-BOX-3 不再作为 active target。历史 board、ES7210 诊断代码、构建补丁脚本和
硬件问题文档集中保留在：

```text
archive/esp32s3-box-3/
```

当前真机构建、烧录、麦克风采集、OLED 显示和模型验证均以 `esp32s3-devkit` 为准。

## Manifest 建议

如果后续把 `ccf_audio` 仓库作为独立参赛仓复现，建议在 manifest 中加入：

```xml
<linkfile src="app/audio_event"
          dest="apps/examples/audio_event"/>
<linkfile src="app/audio_record"
          dest="apps/examples/audio_record"/>
<linkfile src="app/audio_test"
          dest="apps/examples/audio_test"/>
<linkfile src="board/esp32s3-devkit"
          dest="vendor/espressif/boards/esp32s3/esp32s3-devkit"/>
<linkfile src="board/goldfish-arm64/configs/audio_event/defconfig"
          dest="vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig"/>
<linkfile src="board/goldfish-arm64/configs/audio_event/config.ini"
          dest="vendor/openvela/boards/vela/configs/goldfish-audio_event/config.ini"/>
```

## 常见问题和排障

常见运行、采集、模拟器和 ESP32-S3 构建问题统一放在
[`docs/使用与调试/常见问题.md`](docs/使用与调试/常见问题.md)。
