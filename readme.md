# ccf_audioevent

`ccf_audioevent` 是一个面向 openvela 的本地音频事件检测作品目录，核心目标是在
模拟器和 ESP32-S3 DevKit + INMP441 真机上运行 `audio_event` 应用，完成音频采集、
特征提取、TFLite Micro 推理和本地告警闭环。

## 目录结构

```text
ccf_audioevent/
├── app/audio_event/                  # audio_event 应用源码
├── app/audio_record/                 # 短录音 WAV base64 导出工具
├── app/audio_test/                   # 麦克风 PCM 采集诊断工具
├── board/esp32s3-box-3/              # ESP32-S3-BOX-3 自定义板级适配
├── board/esp32s3-devkit/             # ESP32-S3 DevKit + INMP441 自定义板级适配
├── board/goldfish-arm64/configs/
│   └── audio_event/defconfig         # goldfish-arm64 模拟器配置源文件
├── scripts/                          # 构建、字体和 BOX-3 修复辅助脚本
└── readme.md                         # 本文档
```

应用源码最终需要映射到 openvela 工作区的 `apps/examples/audio_event`。
录音导出工具最终需要映射到 `apps/examples/audio_record`。
麦克风诊断工具最终需要映射到 `apps/examples/audio_test`。
模拟器配置最终需要出现在 `vendor/openvela/boards/vela/configs/goldfish-audio_event`。
ESP32-S3-BOX-3 板级代码最终需要映射到
`vendor/espressif/boards/esp32s3/esp32s3-box-3`。
ESP32-S3 DevKit + INMP441 板级代码最终需要映射到
`vendor/espressif/boards/esp32s3/esp32s3-devkit`。

## 工作区软链接

在 openvela 工作区根目录执行以下命令，将 `ccf_audioevent` 中的源码挂到构建系统能找到
的位置。

```bash
ln -sfnT /home/arongw/openvela/ccf_audioevent/app/audio_event \
  /home/arongw/openvela/apps/examples/audio_event

ln -sfnT /home/arongw/openvela/ccf_audioevent/app/audio_record \
  /home/arongw/openvela/apps/examples/audio_record

ln -sfnT /home/arongw/openvela/ccf_audioevent/app/audio_test \
  /home/arongw/openvela/apps/examples/audio_test

mkdir -p /home/arongw/openvela/vendor/openvela/boards/vela/configs/goldfish-audio_event
rm -f /home/arongw/openvela/vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig
ln -sfn /home/arongw/openvela/ccf_audioevent/board/goldfish-arm64/configs/audio_event/defconfig \
  /home/arongw/openvela/vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig

ln -sfnT /home/arongw/openvela/ccf_audioevent/board/esp32s3-box-3 \
  /home/arongw/openvela/vendor/espressif/boards/esp32s3/esp32s3-box-3

ln -sfnT /home/arongw/openvela/ccf_audioevent/board/esp32s3-devkit \
  /home/arongw/openvela/vendor/espressif/boards/esp32s3/esp32s3-devkit
```

检查软链接：

```bash
readlink -f apps/examples/audio_event
readlink -f apps/examples/audio_record
readlink -f apps/examples/audio_test
readlink -f vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig
readlink -f vendor/espressif/boards/esp32s3/esp32s3-box-3
readlink -f vendor/espressif/boards/esp32s3/esp32s3-devkit
```

期望 `apps/examples/audio_event`、`apps/examples/audio_record`、
`apps/examples/audio_test`、
`goldfish-audio_event/defconfig` 和
`esp32s3-box-3`、`esp32s3-devkit` 都指向
`/home/arongw/openvela/ccf_audioevent/...`。

`audio_record` 和 `audio_test` 是新增 example。`apps/examples/Kconfig` 是自动生成文件，
先确保上面的 `apps/examples/audio_record`、`apps/examples/audio_test` 软链接存在，再重新
configure 或构建，让 Kconfig 生成阶段自动加入：

```text
source "/home/arongw/openvela/apps/examples/audio_record/Kconfig"
source "/home/arongw/openvela/apps/examples/audio_test/Kconfig"
```

如果刷新配置后仍看不到 `CONFIG_EXAMPLES_AUDIO_RECORD` 或 `CONFIG_EXAMPLES_AUDIO_TEST`，
优先检查软链接是否存在，而不是长期手工维护 `apps/examples/Kconfig`。

注意：`goldfish-audio_event` 目录本身必须保留为
`vendor/openvela/boards/vela/configs/` 下的真实目录，只软链接其中的 `defconfig`
文件。不要把整个 `goldfish-audio_event` 目录软链接到 `ccf_audioevent`，否则非 CMake
构建会把它当成完整 board 路径，导致 `File Make.defs could not be found`。

## 模拟器构建

goldfish-arm64 模拟器配置使用 openvela 的通用 `vela` board，因此最终配置路径放在：

```text
vendor/openvela/boards/vela/configs/goldfish-audio_event/
```

推荐使用 CMake 构建：

```bash
./build.sh vendor/openvela/boards/vela/configs/goldfish-audio_event/ --cmake -j8
```

CMake 构建后，输出目录为：

```text
cmake_out/vela_goldfish-audio_event/
```

运行模拟器：

```bash
./emulator.sh cmake_out/vela_goldfish-audio_event/
```

也可以使用 make 构建：

```bash
./build.sh vendor/openvela/boards/vela/configs/goldfish-audio_event/ -j8
```

make 构建后，产物主要在 `nuttx/` 下；如果要用 `emulator.sh`，需要手动整理一个
out 目录：

```bash
mkdir -p cmake_out/vela_goldfish-audio_event
cp nuttx/.config cmake_out/vela_goldfish-audio_event/
cp nuttx/vela_*.bin cmake_out/vela_goldfish-audio_event/
cp ccf_audioevent/board/goldfish-arm64/configs/audio_event/config.ini \
  cmake_out/vela_goldfish-audio_event/
rm -f cmake_out/vela_goldfish-audio_event/nuttx
ln -s ../../nuttx/nuttx cmake_out/vela_goldfish-audio_event/nuttx

./emulator.sh cmake_out/vela_goldfish-audio_event/
```

`config.ini` 将 goldfish 显示固定为 `320x240` 横向模式，用于对齐
ESP32-S3-BOX-3 的小屏 UI。若之前已经启动过模拟器并生成了旧的
`hardware-qemu.ini`，需要重新启动模拟器；必要时删除旧的
`cmake_out/vela_goldfish-audio_event/hardware-qemu.ini` 后再启动。

进入 NSH 后可先验证应用是否注册：

```text
nsh> help | grep audio_event
nsh> audio_event --model-smoke
```

模拟器更适合做模型加载、文件输入和 UI 基础验证。默认采集设备配置为
`/dev/audio/pcm0c`，如设备节点不存在，可优先使用文件模式：

```text
nsh> audio_event --file /data/res/audio/cough_2.wav --repeat 100
```

## ESP32-S3-BOX-3 构建（保留适配）

ESP32-S3-BOX-3 适配仍保留在仓库中，主要用于回溯 ES7210 和 BOX-3 板级问题。
当前真机主路径使用后面的 `esp32s3-devkit` 配置。

BOX-3 使用自定义 board：

```text
vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/audio_event/
```

构建命令：

```bash
./build.sh vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/audio_event/ --cmake -j8
```

烧录并打开串口监视：

```bash
cd nuttx && make flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BAUD=921600 && cd ..
picocom -b 115200 /dev/ttyACM0
```

如果开发板枚举为其他串口设备，请将 `/dev/ttyACM0` 替换为实际端口。

该配置启用 ES7210 麦克风初始化，并将 I2S1 RX 注册为：

```text
/dev/audio/pcm_in1
```

烧录启动后可验证：

```text
nsh> help | grep audio_event
nsh> audio_event --model-smoke
nsh> audio_event --device /dev/audio/pcm_in1 --audio-stats --once
nsh> audio_event --device /dev/audio/pcm_in1
```

## 真机构建（ESP32-S3 DevKit + INMP441）

当前真机构建使用 ESP32-S3-N16R8 DevKit 外接 INMP441 数字麦克风，使用独立的
custom board：

```text
vendor/espressif/boards/esp32s3/esp32s3-devkit/configs/audio_event/
```

当前默认接线：

| INMP441 引脚 | ESP32-S3 DevKit 连接 | 说明 / 配置项 |
| --- | --- | --- |
| VDD | 3V3 | 使用 3.3 V 供电，不要接 5 V |
| GND | GND | 与开发板共地 |
| SCK / BCLK | GPIO18 | `CONFIG_ESP32S3_I2S1_BCLKPIN=18` |
| WS / LRCK | GPIO17 | `CONFIG_ESP32S3_I2S1_WSPIN=17` |
| SD / DOUT | GPIO15 | `CONFIG_ESP32S3_I2S1_DINPIN=15` |
| L/R | GND | 选择 Left slot，对应 `CONFIG_EXAMPLES_AUDIO_EVENT_INMP441_SLOT=0` |

INMP441 不需要 MCLK，当前 devkit 配置只使用 I2S1 的 `BCLK`、`WS/LRCK` 和 `DIN`
三根音频信号线。若将 `L/R` 接到 3V3，应把应用侧 slot 改为
`CONFIG_EXAMPLES_AUDIO_EVENT_INMP441_SLOT=1` 后重新构建。

0.96 寸 I2C OLED 用于做最小显示闭环，当前按 SSD1306 128x64、7-bit I2C 地址
`0x3C` 配置：

| OLED 引脚 | ESP32-S3 DevKit 连接 | 说明 / 配置项 |
| --- | --- | --- |
| VCC | 3V3 | 使用 3.3 V 供电 |
| GND | GND | 与开发板共地 |
| SCL | GPIO5 | `CONFIG_ESP32S3_I2C0_SCLPIN=5` |
| SDA | GPIO4 | `CONFIG_ESP32S3_I2C0_SDAPIN=4` |
| I2C 地址 | `0x3C` | `CONFIG_SSD1306_I2CADDR=60` |

启动后 board bring-up 会初始化 I2C0 和 OLED，并在屏幕上显示：

```text
CCF AUDIO
OLED OK
I2C 0x3C
```

看到这三行字，说明 3V3/GND、I2C0 SDA/SCL、OLED 地址和 SSD1306 基础初始化已经形成
最小闭环。如果屏幕亮但内容错位或无字，先确认模块是否为 SH1106 兼容屏；这类屏常见为
132 列内部显存，后续需要把 OLED 型号配置从 `CONFIG_LCD_UG2864HSWEG01` 调整到
对应的 SH1106 配置。

该配置启用 I2S1 RX，并将采集设备注册为：

```text
/dev/audio/pcm_in1
```

构建命令：

```bash
./build.sh vendor/espressif/boards/esp32s3/esp32s3-devkit/configs/audio_event/ -j8
```

烧录并打开串口监视：

```bash
cd nuttx && make flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BAUD=921600 && cd ..
picocom -b 115200 /dev/ttyACM0
```

启动后先验证命令是否注册：

```text
nsh> help | grep audio
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 2
nsh> audio_test --device /dev/audio/pcm_in1 --channels 2 --seconds 5
nsh> audio_event --model-smoke
nsh> audio_event --device /dev/audio/pcm_in1 --audio-stats
```

`audio_record` 会先在板端缓存一小段录音，采集完成后再在串口中输出标准 WAV 的
base64 文本：

```text
WAV_BASE64_BEGIN
...
WAV_BASE64_END
```

在电脑端只复制两行 marker 中间的 base64 内容到 `record.b64`，然后解码：

```bash
base64 -d record.b64 > record.wav
```

也可以让 `picocom` 直接保存串口日志，避免手工复制大段 base64：

```bash
picocom -b 115200 /dev/ttyACM0 --logfile audio_record.log
```

在 NSH 中运行录音命令：

```text
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 2
```

验证 PSRAM 长录音时可运行：

```text
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 60
```

退出 `picocom` 后，从日志中提取 marker 中间的 base64 并生成 WAV：

```bash
sed -n '/WAV_BASE64_BEGIN/,/WAV_BASE64_END/p' audio_record.log \
  | sed '1d;$d' \
  | tr -d '\r' \
  | base64 -d > record.wav
```

检查 WAV 格式：

```bash
file record.wav
ls -lh record.wav
```

正常应显示为 16 kHz、mono、16-bit PCM WAV。播放可使用：

```bash
aplay record.wav
```

如果没有 `aplay`，也可以使用 `ffplay record.wav` 或 Audacity 打开。注意
`record.b64` 中只能保留 base64 正文，不要混入 `WAV_BASE64_BEGIN`、
`WAV_BASE64_END`、`nsh>` 提示符或 `[audio_record]` 日志。

默认录制 2 秒、16 kHz、mono、int16 WAV。当前 devkit 配置已启用 N16R8 的
octal PSRAM，并把 PSRAM 加入 common heap，`audio_record` 可通过 `--seconds N`
最长录制 60 秒。60 秒 mono int16 录音缓存约占：

```text
16000 samples/s * 60 s * 2 bytes = 1,920,000 bytes
```

`audio_record` 仍然是“先采集到板端内存，采集完成后再输出 base64”，不是边录边往串口
实时输出。这样 WAV 头里的数据长度是确定的，电脑端提取出的 `record.wav` 更稳定。长录音
会在采集完成后输出很长一段 base64，建议用 `picocom --logfile` 保存串口日志后再解码。

如果运行 60 秒录音时提示 `cannot allocate ... bytes`，先确认本次固件确实由
`esp32s3-devkit/configs/audio_event/defconfig` 重新 configure，并在启动日志或
`nsh> free` 中确认 PSRAM 已作为额外 heap 可用。

如果 `audio_test` 或 `audio_event --audio-stats` 已经显示非零数据，说明
INMP441 到 ESP32-S3 I2S RX 的硬件链路基本打通。若 `audio_event` 日志中出现以下内容，
说明应用侧已经启用 INMP441 适配，会把 32-bit I2S slot 转成模型需要的
16 kHz mono PCM16：

```text
[audio] configure input pcm rate=16000 channels=2 bits=32
[audio] INMP441 adapter: slot=0 shift=16 output=mono int16
```

当前 devkit 配置保留：

```text
CONFIG_ESP32S3_I2S1_DATA_BIT_WIDTH_32BIT=y
CONFIG_EXAMPLES_AUDIO_EVENT_INMP441_32BIT=y
CONFIG_EXAMPLES_AUDIO_EVENT_INMP441_SLOT=0
CONFIG_EXAMPLES_AUDIO_EVENT_INMP441_SHIFT=16
```

其中 `SLOT=0` 对应当前 `L/R` 接 GND 的左声道；`SHIFT=16` 是 32-bit 样本转
16-bit PCM 的初始缩放值。若安静环境下零值比例过高且声音细节偏弱，可后续尝试
`SHIFT=15` 或 `SHIFT=14` 做幅度标定；若出现长期削波，再调回更大的右移值。

### 麦克风诊断

`audio_test` 是独立的 PCM 采集诊断命令，不加载模型、不初始化 LVGL，只验证
`/dev/audio/pcm_in1` 是否能输出有效的 16-bit PCM。它每秒打印一次每个声道的
`min/max/mean/rms/zero/clip`：

```text
nsh> audio_test --device /dev/audio/pcm_in1 --channels 1 --seconds 5
nsh> audio_test --device /dev/audio/pcm_in1 --channels 2 --seconds 5
```

如果怀疑 ES7210 初始化早于 I2S 时钟启动，可以使用启动后重初始化诊断：

```text
nsh> audio_test --device /dev/audio/pcm_in1 --channels 2 --seconds 10 --es7210
```

该选项会在 `audio_test` 启动采集后等待 50 ms，再重新执行一次 BOX-3 ES7210
初始化序列。如果重初始化后的后续秒数从全零变为非零，说明根因高度指向
ES7210 初始化时 I2S `MCLK/BCLK/LRCK` 尚未稳定。

结果判断：

- `rms` 长期接近 0 且 `zero` 接近总采样数：驱动链路可能没有收到麦克风数据。
- 单声道全零、双声道某一路有 `rms`：重点检查 I2S slot/channel 配置。
- `clip` 持续增加：输入增益过高或格式解释错误。
- 对着麦克风敲击或说话时 `rms` 明显升高：采集链路基本可用，再回到
  `audio_event` 做模型和阈值验证。

## 关键配置

`board/goldfish-arm64/configs/audio_event/defconfig` 主要启用：

- `CONFIG_EXAMPLES_AUDIO_EVENT=y`
- `CONFIG_EXAMPLES_AUDIO_EVENT_DEVPATH="/dev/audio/pcm0c"`
- `CONFIG_EXAMPLES_AUDIO_EVENT_UI=y`
- `CONFIG_TFLITEMICRO=y`
- `CONFIG_MATH_KISSFFT=y`
- `CONFIG_SYSTEM_FLATBUFFERS=y`

该配置已经移除与 `audio_event` 无关的重型模拟器组件，避免引入不必要的构建依赖：

- Android Binder / ServiceManager
- QuickApp / Feature Framework
- Media server / media tool
- curl command line

`board/esp32s3-box-3/configs/audio_event/defconfig` 主要启用：

- `CONFIG_EXAMPLES_AUDIO_EVENT=y`
- `CONFIG_EXAMPLES_AUDIO_EVENT_DEVPATH="/dev/audio/pcm_in1"`
- `CONFIG_EXAMPLES_AUDIO_TEST=y`
- `CONFIG_EXAMPLES_AUDIO_TEST_DEVPATH="/dev/audio/pcm_in1"`
- `CONFIG_ESP32S3_BOX_AUDIO=y`
- `CONFIG_ESP32S3_I2S1_DINPIN=16`
- `CONFIG_ESP32S3_I2S1_TX` 未启用，麦克风验证阶段只保留 RX
- `CONFIG_ESP32S3_BOX_LCD=y`
- `CONFIG_ESP32S3_BOARD_TOUCHSCREEN=y`
- `CONFIG_TFLITEMICRO=y`
- `CONFIG_MATH_KISSFFT=y`

`board/esp32s3-devkit/configs/audio_event/defconfig` 主要启用：

- `CONFIG_EXAMPLES_AUDIO_EVENT=y`
- `CONFIG_EXAMPLES_AUDIO_EVENT_DEVPATH="/dev/audio/pcm_in1"`
- `CONFIG_EXAMPLES_AUDIO_EVENT_INMP441_32BIT=y`
- `CONFIG_EXAMPLES_AUDIO_EVENT_INMP441_SLOT=0`
- `CONFIG_EXAMPLES_AUDIO_EVENT_INMP441_SHIFT=16`
- `CONFIG_EXAMPLES_AUDIO_RECORD=y`
- `CONFIG_EXAMPLES_AUDIO_RECORD_DEVPATH="/dev/audio/pcm_in1"`
- `CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_32BIT=y`
- `CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_SLOT=0`
- `CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_SHIFT=16`
- `CONFIG_EXAMPLES_AUDIO_TEST=y`
- `CONFIG_EXAMPLES_AUDIO_TEST_CHANNELS=2`
- `CONFIG_ESP32S3_DEVKIT_INMP441=y`
- `CONFIG_ESP32S3_DEVKIT_OLED=y`
- `CONFIG_ESP32S3_I2C0_MASTER_MODE=y`
- `CONFIG_ESP32S3_I2C0_SCLPIN=5`
- `CONFIG_ESP32S3_I2C0_SDAPIN=4`
- `CONFIG_ESP32S3_I2S1_BCLKPIN=18`
- `CONFIG_ESP32S3_I2S1_WSPIN=17`
- `CONFIG_ESP32S3_I2S1_DINPIN=15`
- `CONFIG_ESP32S3_I2S1_DATA_BIT_WIDTH_32BIT=y`
- `CONFIG_LCD_SSD1306_I2C=y`
- `CONFIG_LCD_UG2864HSWEG01=y`
- `CONFIG_SSD1306_I2CADDR=60`
- `CONFIG_LIBCXX=y`
- `CONFIG_TLS_NELEM=4`
- `CONFIG_TLS_TASK_NELEM=4`
- `CONFIG_TFLITEMICRO=y`
- `CONFIG_MATH_KISSFFT=y`

## Manifest 建议

如果后续把 `ccf_audioevent` 作为独立参赛仓复现，建议在 manifest 中加入：

```xml
<linkfile src="app/audio_event"
          dest="apps/examples/audio_event"/>
<linkfile src="app/audio_record"
          dest="apps/examples/audio_record"/>
<linkfile src="app/audio_test"
          dest="apps/examples/audio_test"/>
<linkfile src="board/goldfish-arm64/configs/audio_event/defconfig"
          dest="vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig"/>
<linkfile src="board/goldfish-arm64/configs/audio_event/config.ini"
          dest="vendor/openvela/boards/vela/configs/goldfish-audio_event/config.ini"/>
<linkfile src="board/esp32s3-box-3"
          dest="vendor/espressif/boards/esp32s3/esp32s3-box-3"/>
<linkfile src="board/esp32s3-devkit"
          dest="vendor/espressif/boards/esp32s3/esp32s3-devkit"/>
```

这样评委或其他开发者同步仓库后，不需要手工复制文件，只要使用对应的构建路径即可。

## 注意事项

- `ccf_audioevent/app/audio_event/.git` 已清理，避免应用目录成为嵌套 Git 仓库。
- goldfish-arm64 的配置不是完整 board，只是 `vendor/openvela/boards/vela` 的一个
  config，因此不要直接用 `ccf_audioevent/board/goldfish-arm64/configs/audio_event/`
  作为 `build.sh` 路径。
- 非 CMake 构建不会自动准备 `cmake_out/vela_goldfish-audio_event/`，运行模拟器前需要
  手动复制 `.config`、`vela_*.bin` 并链接 `nuttx`。
- 当前真机构建目标是 ESP32-S3 DevKit + INMP441，配置路径为
  `vendor/espressif/boards/esp32s3/esp32s3-devkit/configs/audio_event/`。
  BOX-3 配置仅作为保留适配。
- 真机音频采集路径是 `/dev/audio/pcm_in1`，模拟器默认路径是 `/dev/audio/pcm0c`。
- ESP32-S3 DevKit + INMP441 的 `audio_event` 已按 32-bit I2S slot 采集，并在应用层
  转成模型输入所需的 16-bit mono PCM。若更改 INMP441 的 `L/R` 接法，需要同步调整
  `CONFIG_EXAMPLES_AUDIO_EVENT_INMP441_SLOT`。
- 若 UI 初始化失败，`audio_event` 会继续运行，可先用 `--model-smoke` 或 `--file`
  模式确认推理链路。

## 常见问题

### File Make.defs could not be found

通常是把整个 `vendor/openvela/boards/vela/configs/goldfish-audio_event` 目录软链接到了
`ccf_audioevent/board/goldfish-arm64/configs/audio_event`。修复方式是删除该目录软链，
创建真实目录，并只软链接 `defconfig` 文件。

### Binder AIDL target 重复

如果出现 `android_binder_IServiceManager` target 重复，说明配置里仍启用了 Android
Binder。`audio_event` 不依赖 Binder，应确认 defconfig 中没有
`CONFIG_ANDROID_BINDER=y`、`CONFIG_ANDROID_SERVICEMANAGER=y`、`CONFIG_BINDER_EXAMPLES=y`
和 `CONFIG_DRIVERS_BINDER=y`。

### CONFIG_HAP_APP_PATH 未定义

如果 `feature/audio_impl.c` 报 `CONFIG_HAP_APP_PATH` 未定义，说明配置仍拉入了
QuickApp / Feature Framework / Media server。`audio_event` 模拟器配置不需要这些框架，
应确认 defconfig 中没有 `CONFIG_QUICKAPP=y`、`CONFIG_FEATURE_FRAMEWORK=y`、
`CONFIG_MEDIA_SERVER=y` 和 `CONFIG_MEDIA_TOOL=y`。
