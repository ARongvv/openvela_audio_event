# ccf_audioevent

`ccf_audioevent` 是一个面向 openvela 的本地音频事件检测作品目录，核心目标是在
模拟器和 ESP32-S3-BOX-3 真机上运行 `audio_event` 应用，完成音频采集、特征提取、
TFLite Micro 推理和本地告警闭环。

## 目录结构

```text
ccf_audioevent/
├── app/audio_event/                  # audio_event 应用源码
├── board/esp32s3-box-3/              # ESP32-S3-BOX-3 自定义板级适配
├── board/goldfish-arm64/configs/
│   └── audio_event/defconfig         # goldfish-arm64 模拟器配置源文件
├── scripts/                          # 构建、字体和 BOX-3 修复辅助脚本
└── readme.md                         # 本文档
```

应用源码最终需要映射到 openvela 工作区的 `apps/examples/audio_event`。
模拟器配置最终需要出现在 `vendor/openvela/boards/vela/configs/goldfish-audio_event`。
ESP32-S3-BOX-3 板级代码最终需要映射到
`vendor/espressif/boards/esp32s3/esp32s3-box-3`。

## 工作区软链接

在 openvela 工作区根目录执行以下命令，将 `ccf_audioevent` 中的源码挂到构建系统能找到
的位置。

```bash
ln -sfnT /home/arongw/openvela/ccf_audioevent/app/audio_event \
  /home/arongw/openvela/apps/examples/audio_event

mkdir -p /home/arongw/openvela/vendor/openvela/boards/vela/configs/goldfish-audio_event
rm -f /home/arongw/openvela/vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig
ln -sfn /home/arongw/openvela/ccf_audioevent/board/goldfish-arm64/configs/audio_event/defconfig \
  /home/arongw/openvela/vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig

ln -sfnT /home/arongw/openvela/ccf_audioevent/board/esp32s3-box-3 \
  /home/arongw/openvela/vendor/espressif/boards/esp32s3/esp32s3-box-3
```

检查软链接：

```bash
readlink -f apps/examples/audio_event
readlink -f vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig
readlink -f vendor/espressif/boards/esp32s3/esp32s3-box-3
```

期望 `apps/examples/audio_event`、`goldfish-audio_event/defconfig` 和
`esp32s3-box-3` 都指向 `/home/arongw/openvela/ccf_audioevent/...`。

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

## 真机构建

ESP32-S3-BOX-3 使用自定义 board：

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

该配置启用 ES7210 麦克风初始化，并将 I2S0 RX 注册为：

```text
/dev/audio/pcm_in0
```

烧录启动后可验证：

```text
nsh> help | grep audio_event
nsh> audio_event --model-smoke
nsh> audio_event --device /dev/audio/pcm_in0 --audio-stats --once
nsh> audio_event --device /dev/audio/pcm_in0
```

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
- `CONFIG_EXAMPLES_AUDIO_EVENT_DEVPATH="/dev/audio/pcm_in0"`
- `CONFIG_ESP32S3_BOX_AUDIO=y`
- `CONFIG_ESP32S3_BOX_LCD=y`
- `CONFIG_ESP32S3_BOARD_TOUCHSCREEN=y`
- `CONFIG_TFLITEMICRO=y`
- `CONFIG_MATH_KISSFFT=y`

## Manifest 建议

如果后续把 `ccf_audioevent` 作为独立参赛仓复现，建议在 manifest 中加入：

```xml
<linkfile src="app/audio_event"
          dest="apps/examples/audio_event"/>
<linkfile src="board/goldfish-arm64/configs/audio_event/defconfig"
          dest="vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig"/>
<linkfile src="board/goldfish-arm64/configs/audio_event/config.ini"
          dest="vendor/openvela/boards/vela/configs/goldfish-audio_event/config.ini"/>
<linkfile src="board/esp32s3-box-3"
          dest="vendor/espressif/boards/esp32s3/esp32s3-box-3"/>
```

这样评委或其他开发者同步仓库后，不需要手工复制文件，只要使用对应的构建路径即可。

## 注意事项

- `ccf_audioevent/app/audio_event/.git` 已清理，避免应用目录成为嵌套 Git 仓库。
- goldfish-arm64 的配置不是完整 board，只是 `vendor/openvela/boards/vela` 的一个
  config，因此不要直接用 `ccf_audioevent/board/goldfish-arm64/configs/audio_event/`
  作为 `build.sh` 路径。
- 非 CMake 构建不会自动准备 `cmake_out/vela_goldfish-audio_event/`，运行模拟器前需要
  手动复制 `.config`、`vela_*.bin` 并链接 `nuttx`。
- 真机音频采集路径是 `/dev/audio/pcm_in0`，模拟器默认路径是 `/dev/audio/pcm0c`。
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
