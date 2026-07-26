# goldfish 模拟器构建和运行

goldfish-arm64 模拟器主要用于文件输入、模型加载和 320x240 LVGL dashboard 验证。它不是
当前真机主路径。

## 配置路径

最终配置路径：

```text
vendor/openvela/boards/vela/configs/goldfish-audio_event/
```

`goldfish-audio_event` 目录本身必须是
`vendor/openvela/boards/vela/configs/` 下的真实目录，只软链接其中的 `defconfig`。
不要把整个 `goldfish-audio_event` 目录软链接到 `ccf_audio`，否则非 CMake 构建会
报 `File Make.defs could not be found`。

```bash
mkdir -p "$OPENVELA_ROOT/vendor/openvela/boards/vela/configs/goldfish-audio_event"
rm -f "$OPENVELA_ROOT/vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig"
ln -sfn "$CCF_AUDIO_ROOT/board/goldfish-arm64/configs/audio_event/defconfig" \
  "$OPENVELA_ROOT/vendor/openvela/boards/vela/configs/goldfish-audio_event/defconfig"
```

## CMake 构建

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

## make 构建

也可以使用 make 构建：

```bash
./build.sh vendor/openvela/boards/vela/configs/goldfish-audio_event/ -j8
```

make 构建后若要运行 `emulator.sh`，需要整理 out 目录：

```bash
mkdir -p cmake_out/vela_goldfish-audio_event
cp nuttx/.config cmake_out/vela_goldfish-audio_event/
cp nuttx/vela_*.bin cmake_out/vela_goldfish-audio_event/
cp ccf_audio/board/goldfish-arm64/configs/audio_event/config.ini \
  cmake_out/vela_goldfish-audio_event/
rm -f cmake_out/vela_goldfish-audio_event/nuttx
ln -s ../../nuttx/nuttx cmake_out/vela_goldfish-audio_event/nuttx

./emulator.sh cmake_out/vela_goldfish-audio_event/
```

`config.ini` 将 goldfish 显示固定为 `320x240` 横向模式。若之前启动过模拟器并生成了旧
`hardware-qemu.ini`，必要时删除
`cmake_out/vela_goldfish-audio_event/hardware-qemu.ini` 后再启动。
