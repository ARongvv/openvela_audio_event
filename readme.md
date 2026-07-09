# ccf_audioevent

`ccf_audioevent` 是一个面向 openvela 的本地音频事件检测作品目录。当前主路径是
`ESP32-S3-N16R8 DevKit + INMP441 数字麦克风 + 0.96 寸 I2C OLED`，核心应用是
`audio_event`：采集音频、提取 log-mel/delta 特征、运行 TFLite Micro 模型，并在
OLED 上显示检测状态。

`audio_record` 和 `audio_test` 是辅助工具：前者用于导出实录 WAV，后者用于检查
I2S/INMP441 采集质量。goldfish 模拟器仍保留，用于文件输入、模型加载和大屏 LVGL UI
验证，但放在真机主流程之后。

## 目录结构

```text
ccf_audioevent/
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
├── scripts/                          # 字体等辅助脚本
└── readme.md
```

## 工作区软链接

在 openvela 根目录执行：

```bash
ln -sfnT /home/arongw/openvela/ccf_audioevent/app/audio_event \
  /home/arongw/openvela/apps/examples/audio_event

ln -sfnT /home/arongw/openvela/ccf_audioevent/app/audio_record \
  /home/arongw/openvela/apps/examples/audio_record

ln -sfnT /home/arongw/openvela/ccf_audioevent/app/audio_test \
  /home/arongw/openvela/apps/examples/audio_test

ln -sfnT /home/arongw/openvela/ccf_audioevent/board/esp32s3-devkit \
  /home/arongw/openvela/vendor/espressif/boards/esp32s3/esp32s3-devkit

mkdir -p /home/arongw/openvela/vendor/openvela/boards/vela/configs/goldfish-audio_event
rm -f /home/arongw/openvela/vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig
ln -sfn /home/arongw/openvela/ccf_audioevent/board/goldfish-arm64/configs/audio_event/defconfig \
  /home/arongw/openvela/vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig
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
不要把整个 `goldfish-audio_event` 目录软链接到 `ccf_audioevent`，否则非 CMake 构建会
报 `File Make.defs could not be found`。

`audio_record` 和 `audio_test` 是新增 example。软链接存在后重新 configure 或构建，
`apps/examples/Kconfig` 生成阶段会自动加入：

```text
source "/home/arongw/openvela/apps/examples/audio_record/Kconfig"
source "/home/arongw/openvela/apps/examples/audio_test/Kconfig"
```

如果 NSH 中没有 `audio_record` 或 `audio_test`，优先检查软链接和重新 configure 状态。

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
./build.sh vendor/espressif/boards/esp32s3/esp32s3-devkit/configs/audio_event/ -j8
```

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

## audio_record 录音导出工具

`audio_record` 用来把真机采集到的 INMP441 音频导出为 WAV，方便在电脑上听、看波形、
检查削波和静音段。它不是主检测应用。

短录音：

```text
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 2
```

最长录音由 `CONFIG_EXAMPLES_AUDIO_RECORD_MAX_SECONDS` 控制，当前为 60 秒：

```text
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 60
```

也可以临时指定 INMP441 slot 和缩放：

```text
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 3 --slot 0 --shift 16
```

`audio_record` 不会自动把文件保存到电脑。它会先在板端内存中缓存录音，采集完成后在
串口输出 WAV 的 base64：

```text
WAV_BASE64_BEGIN
...
WAV_BASE64_END
```

电脑端只复制两行 marker 中间的 base64 正文到 `record.b64`，然后解码：

```bash
base64 -d record.b64 > record.wav
```

长录音建议用 `picocom --logfile` 保存串口日志：

```bash
picocom -b 115200 /dev/ttyACM0 --logfile audio_record.log
```

NSH 中执行：

```text
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 60
```

退出 `picocom` 后提取 WAV：

```bash
sed -n '/WAV_BASE64_BEGIN/,/WAV_BASE64_END/p' audio_record.log \
  | sed '1d;$d' \
  | tr -d '\r' \
  | base64 -d > record.wav
```

检查和播放：

```bash
file record.wav
ls -lh record.wav
aplay record.wav
```

正常应是 16 kHz、mono、16-bit PCM WAV。60 秒缓存约占：

```text
16000 samples/s * 60 s * 2 bytes = 1,920,000 bytes
```

因此长录音依赖 ESP32-S3-N16R8 的 PSRAM 已加入 heap。如果提示分配失败，先确认固件是
用 `esp32s3-devkit/configs/audio_event` 重新 configure 并烧录的。

注意：`record.b64` 中只能保留 base64 正文，不要混入 `WAV_BASE64_BEGIN`、
`WAV_BASE64_END`、`nsh>` 或 `[audio_record]` 日志。

## audio_test 采集诊断工具

`audio_test` 只检查音频采集，不加载模型、不跑 OLED UI。它适合排查硬件接线、I2S slot、
位宽转换和削波问题。

默认测试：

```text
nsh> audio_test --device /dev/audio/pcm_in1 --seconds 5
```

显式指定 2 声道诊断：

```text
nsh> audio_test --device /dev/audio/pcm_in1 --channels 2 --seconds 5
```

INMP441 适配启用时，日志会同时报告 `slot0`、`slot1` 和最终 `mono(slot0)`：

```text
[audio_test] INMP441 adapter: slot=0 shift=16 output=mono int16; reporting slot0/slot1/mono
[audio_test] slot0 min=-6486 max=28395 mean=3985 rms=8993 zero=64/16000 clip=0 nearclip=0
[audio_test] slot1 min=0 max=0 mean=0 rms=0 zero=16000/16000 clip=0 nearclip=0
```

判断规则：

- `slot0` 有数据、`slot1` 全 0：符合 `L/R` 接 GND 的 INMP441。
- 两路都全 0：检查 VDD/GND/BCLK/WS/SD 接线和 `/dev/audio/pcm_in1`。
- 数据在另一 slot：检查 `L/R` 接法或把 slot 改为 1。
- `clip` 或 `nearclip` 持续增加：输入过大或右移太小，优先增大 shift。
- 敲击或咳嗽时 `rms` 明显升高：采集链路基本可用，再回到 `audio_event` 验证模型。

## 模拟器构建和运行

goldfish-arm64 模拟器主要用于文件输入、模型加载和 320x240 LVGL dashboard 验证。它不是
当前真机主路径。

最终配置路径：

```text
vendor/openvela/boards/vela/configs/goldfish-audio_event/
```

推荐 CMake 构建：

```bash
./build.sh vendor/openvela/boards/vela/configs/goldfish-audio_event/ --cmake -j8
```

输出目录：

```text
cmake_out/vela_goldfish-audio_event/
```

运行：

```bash
./emulator.sh cmake_out/vela_goldfish-audio_event/
```

进入 NSH 后：

```text
goldfish-armv8a-ap> audio_event --model-smoke
goldfish-armv8a-ap> audio_event --file /data/res/audio/cough_2.wav --repeat 100
```

模拟器默认采集设备为 `/dev/audio/pcm0c`。如果设备节点或 PulseAudio 状态不稳定，优先用
`--file` 模式验证模型和 UI。

也可以使用 make 构建：

```bash
./build.sh vendor/openvela/boards/vela/configs/goldfish-audio_event/ -j8
```

make 构建后若要运行 `emulator.sh`，需要整理 out 目录：

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

`config.ini` 将 goldfish 显示固定为 `320x240` 横向模式。若之前启动过模拟器并生成了旧
`hardware-qemu.ini`，必要时删除
`cmake_out/vela_goldfish-audio_event/hardware-qemu.ini` 后再启动。

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

如果后续把 `ccf_audioevent` 作为独立参赛仓复现，建议在 manifest 中加入：

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

## 常见问题

### `audio_event` 一直是 silence 或 background

先运行：

```text
nsh> audio_test --device /dev/audio/pcm_in1 --seconds 5
```

确认 slot、rms、zero、clip 正常后，再用：

```text
nsh> audio_event --device /dev/audio/pcm_in1 --audio-stats --profile --no-oled
```

检查模型输入窗口是否有真实声音。如果 `audio_test` 正常而模型不稳定，优先导出 WAV，
听实录音频并检查训练域是否和 INMP441 实录域一致。

### `audio_record` 没有在电脑生成文件

这是正常的。`audio_record` 只通过串口输出 base64，需要在电脑端保存日志并解码为
`record.wav`。

### `File Make.defs could not be found`

通常是把整个 `vendor/openvela/boards/vela/configs/goldfish-audio_event` 目录软链接到了
`ccf_audioevent/board/goldfish-arm64/configs/audio_event`。修复方式是创建真实目录，只软链
`defconfig` 文件。

### Binder AIDL target 重复

说明 goldfish 配置里仍启用了 Android Binder。`audio_event` 不依赖 Binder，应确认没有
`CONFIG_ANDROID_BINDER=y`、`CONFIG_ANDROID_SERVICEMANAGER=y`、`CONFIG_BINDER_EXAMPLES=y`
和 `CONFIG_DRIVERS_BINDER=y`。

### `CONFIG_HAP_APP_PATH` 未定义

说明配置仍拉入了 QuickApp / Feature Framework / Media server。`audio_event` 不需要这些
框架，应确认没有 `CONFIG_QUICKAPP=y`、`CONFIG_FEATURE_FRAMEWORK=y`、
`CONFIG_MEDIA_SERVER=y` 和 `CONFIG_MEDIA_TOOL=y`。
