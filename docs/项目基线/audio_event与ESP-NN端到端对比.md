# audio_event 与 ESP-NN 端到端对比

本文用于在生产 `audio_event` 应用中验证当前 4-class 模型的 ESP-NN 效果。它不同于
`tflm_benchmark`：除了模型 Invoke，还会实际运行音频输入、特征提取、分类、检测和应用日志链路。

## 为什么在 audio_event 中使用 ESP-NN

ESP32-S3 的 Xtensa LX7 不能使用 ARM CMSIS-NN/CMSIS-DSP，也不具备 Xtensa HiFi DSP ISA；ESP-NN 才是与该芯片
匹配的 TFLM 卷积 backend。当前 4-class 全 int8 DS-CNN 的主要计算正是三个 `Conv2D`、两个
`DepthwiseConv2D` 和一个空间 Mean；因此替换这六个经过验证的节点，不改变音频前端、模型输入/输出、类别映射或检测策略。

同模型的受控 CCOUNT 已显示五卷积 + Mean 组合把纯 `Invoke()` mean 从 346.271 ms 降至 17.591 ms（19.6840×），且
各加速节点已完成逐字节 reference 对照、`output_hash` 与 reference 一致。端到端 profile 的目的，是确认这项 kernel 收益在真实特征、文件/麦克风输入、能量门
和应用调度下仍然成立，而不是把 benchmark 结论直接外推到业务链路。代价是 Arena 从 reference 的 22,788 B 增至
44,356 B；因此本文件的三个 profile 保持独立，且替换模型时必须重新 trace 和 verify。正式性能固件对
`combined_A_pure.wav` 的实测为 `feature=60–70 ms`、`infer=20 ms`、`total=80–90 ms`；该口径为业务侧
毫秒计时，仍需按本文的文件长稳和真实采集步骤继续验收。

## 1. 对比 profile

| profile | 用途 | ESP-NN 状态 | WAV 资源 |
| --- | --- | --- | --- |
| `audio_event_espnn_ref_profile` | 公平 reference 对照 | 编入 ESP-NN wrapper，但不选择任何卷积节点 | LittleFS `/data` |
| `audio_event_espnn_verify` | 真实特征的数值验证 | 选择 Conv 23/25/27、DW 24/26 和 Mean 28，开启 TRACE/VERIFY | LittleFS `/data` |
| `audio_event_espnn_profile` | 端到端性能 | 选择相同五个卷积节点和 Mean 28，关闭 TRACE/VERIFY | LittleFS `/data` |

三个 profile 均保持生产 `audio_event` 的 4-class 模型、16 kHz I2S、特征提取、能量门和 65,536 B
Arena 配置。它们将 ESP32-S3 N16R8 的上半区 Flash MTD，即 `0x800000–0xFFFFFF`（8 MiB），配置为
LittleFS，并挂载到 `/data`；
基线 `audio_event` profile 不作修改。ESP-NN 选择仅对当前模型有效；替换模型后必须先用 TRACE 重新确认
tensor ID 和算子形状。

## 2. 制作并烧录板端 WAV 资源

三套 ESP-NN profile 共用同一份 LittleFS 资源镜像，因此 reference、verify 与性能测试能够读取完全相同的
WAV。资源区为 8 MiB，能容纳单个 5 MiB WAV、当前内置样本和 LittleFS 元数据；镜像内文件路径为
`/data/<文件名>`。

仓库已提供可直接使用的 `mklittlefs`：
`vendor/artinchip/tools/scripts/mklittlefs`。当前音频脚本默认从 `PATH` 查找该工具，因此应像下面这样通过
`MKLITTLEFS` 显式指定仓库内版本（该变量也可用于覆盖为其他版本的工具）。生成内置 WAV 的镜像：

```sh
cd ~/openvela
MKLITTLEFS="$PWD/vendor/artinchip/tools/scripts/mklittlefs" \
  bash ccf_audioevent/scripts/make_audio_event_littlefs_image.sh
```

将额外的 5 MiB WAV 一并加入镜像（可传一个或多个 WAV 文件或目录）：

```sh
MKLITTLEFS="$PWD/vendor/artinchip/tools/scripts/mklittlefs" \
  bash ccf_audioevent/scripts/make_audio_event_littlefs_image.sh \
  ccf_audioevent/app/audio_event/res/audio /path/to/event_5mb.wav
```

例如，仅将项目中的 `combined_A_pure.wav` 制作为资源镜像：

```sh
cd ~/openvela
MKLITTLEFS="$PWD/vendor/artinchip/tools/scripts/mklittlefs" \
  bash ccf_audioevent/scripts/make_audio_event_littlefs_image.sh \
  ccf_audioevent/test_data/combined_A_pure.wav
```

先烧录任意一个 LittleFS-enabled ESP-NN firmware，再在主机上单独烧录资源镜像；这一步只覆盖上半区
`0x800000–0xFFFFFF`，不会覆盖放置在下半区的当前固件。若该区域被用于 OTA 或其他数据，必须先重新规划
分区，不能直接执行下面命令：

```sh
esptool --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 write-flash \
  0x800000 ccf_audioevent/out/audio_event_littlefs/audio_event_littlefs.bin
```

复位开发板后，在 NSH 中确认 `ls /data` 能看到文件。以后仅更新 WAV 时，无需重烧固件，只需重新生成并烧录
该资源镜像。请勿将这条资源烧录命令用于基线 `audio_event`，因为它没有挂载 `/data`。

## 3. 真实特征下的组合验证

先构建并烧录 verify profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/audio_event_espnn_verify -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

使用刚烧录到 `/data` 的同一个 WAV，或相同的麦克风场景运行：

```sh
audio_event --file /data/event_5mb.wav --repeat 1 --profile --no-oled

# 或实时采集
audio_event --device /dev/audio/pcm_in1 --once --profile --no-oled
```

每个被实际 Invoke 的窗口必须看到三个 Conv2D、两个 DepthwiseConv2D 和 Mean 的 ESP-NN trace；并至少出现：

```text
[espnn-verify] DepthwiseConv2D out_t=24 match bytes=6000
[espnn-verify] DepthwiseConv2D out_t=26 match bytes=8000
[espnn-verify] Mean out_t=28 match bytes=24
```

同时保存 Conv2D 的 `match` 日志，以及 `[infer]` 的类别和概率输出。若任一节点不匹配，wrapper 会恢复
reference 输出；此时不可将该固件用于性能结论。

## 4. Reference 与 ESP-NN 端到端性能

使用同一块板、相同 CPU 频率、同一 WAV、相同命令参数和 `--no-oled`，分别构建 reference 与性能
profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/audio_event_espnn_ref_profile -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
audio_event --file /data/event_5mb.wav --repeat 1 --profile --no-oled
```

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/audio_event_espnn_profile -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
audio_event --file /data/event_5mb.wav --repeat 1 --profile --no-oled
```

比较 `[profile]` 行中的 `feature`、`infer` 和 `total`。单个约 5 MiB 的 WAV 已含数百个 hop，`--repeat 1`
足以形成常规 P50/P95 性能样本；`--repeat 20` 仅用于约 20 分钟的长稳与积压检查。能量门可能跳过静音窗口的 DSP/TFLM；因此应选用
稳定触发的事件 WAV，并只对实际执行了模型的窗口统计 `infer`。`infer` 包含输入量化、TFLM Invoke 和
输出反量化，适合评价业务应用的模型阶段；它不是纯 Invoke cycle，纯模型基线仍以
`tflm_benchmark` 的 CCOUNT 结果为准。

## 5. 实时验收

固定 WAV 对比通过后，再以实时设备运行：

```sh
audio_event --device /dev/audio/pcm_in1 --profile --no-oled
```

检查以下条件：

- `infer` 和 `total` 的 P95 小于 `CONFIG_EXAMPLES_AUDIO_EVENT_HOP_MS`（当前为 250 ms）；
- 没有音频采集错误、模型 Invoke 失败、重启或协处理器异常；
- 与 reference 固件相比，事件类别、触发次数和误报/漏报没有不可接受的变化；
- 连续运行时不存在窗口积压或逐步增长的处理延迟。

需要低于毫秒粒度的业务侧推理周期时，再为 `audio_event --profile` 增加专用 CCOUNT 字段；不要用
TRACE/VERIFY 固件的耗时代表性能固件。
