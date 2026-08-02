# audio_event 与 ESP-NN 端到端对比

本文用于在生产 `audio_event` 应用中验证当前 4-class 模型的 ESP-NN 效果。它不同于
`tflm_benchmark`：除了模型 Invoke，还会实际运行音频输入、特征提取、分类、检测和应用日志链路。

## 1. 对比 profile

| profile | 用途 | ESP-NN 状态 |
| --- | --- | --- |
| `audio_event_espnn_ref_profile` | 公平 reference 对照 | 编入 ESP-NN wrapper，但不选择任何卷积节点 |
| `audio_event_espnn_verify` | 真实特征的数值验证 | 选择 Conv `23/25/27`、DW `24/26`，开启 TRACE/VERIFY |
| `audio_event_espnn_profile` | 端到端性能 | 选择相同五个节点，关闭 TRACE/VERIFY |

三个 profile 均保持生产 `audio_event` 的 4-class 模型、16 kHz I2S、特征提取、能量门和 65,536 B
Arena 配置。ESP-NN 选择仅对当前模型有效；替换模型后必须先用 TRACE 重新确认 tensor ID 和算子形状。

## 2. 真实特征下的组合验证

先构建并烧录 verify profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/audio_event_espnn_verify -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

使用同一个已知有效的 WAV（HostFS 已配置时）或相同的麦克风场景运行：

```sh
audio_event --file <wav-path> --repeat 1 --profile --no-oled

# 或实时采集
audio_event --device /dev/audio/pcm_in1 --once --profile --no-oled
```

每个被实际 Invoke 的窗口必须看到三个 Conv2D 和两个 DepthwiseConv2D 的 ESP-NN trace；并至少出现：

```text
[espnn-verify] DepthwiseConv2D out_t=24 match bytes=6000
[espnn-verify] DepthwiseConv2D out_t=26 match bytes=8000
```

同时保存 Conv2D 的 `match` 日志，以及 `[infer]` 的类别和概率输出。若任一节点不匹配，wrapper 会恢复
reference 输出；此时不可将该固件用于性能结论。

## 3. Reference 与 ESP-NN 端到端性能

使用同一块板、相同 CPU 频率、同一 WAV、相同命令参数和 `--no-oled`，分别构建 reference 与性能
profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/audio_event_espnn_ref_profile -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
audio_event --file <wav-path> --repeat 20 --profile --no-oled
```

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/audio_event_espnn_profile -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
audio_event --file <wav-path> --repeat 20 --profile --no-oled
```

比较 `[profile]` 行中的 `feature`、`infer` 和 `total`。能量门可能跳过静音窗口的 DSP/TFLM；因此应选用
稳定触发的事件 WAV，并只对实际执行了模型的窗口统计 `infer`。`infer` 包含输入量化、TFLM Invoke 和
输出反量化，适合评价业务应用的模型阶段；它不是纯 Invoke cycle，纯模型基线仍以
`tflm_benchmark` 的 CCOUNT 结果为准。

## 4. 实时验收

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
