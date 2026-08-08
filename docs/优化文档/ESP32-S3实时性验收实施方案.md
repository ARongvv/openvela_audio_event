# ESP32-S3 audio_event 实时性验收实施方案

## 1. 目标与范围

本方案验证 4-class int8 DS-CNN 在 ESP32-S3 上的 `audio_event` 业务链路是否能够稳定实时运行。验收对象是
`audio_event_espnn_profile`：三个 Conv2D（23/25/27）、两个 DepthwiseConv2D（24/26）和空间 Mean（28）均使用
ESP-NN 后端；特征提取、量化、分类、检测和告警逻辑保持不变。

实时性的判定单位是一个 hop，而不是单个算子：当前 hop 为 250 ms。一个窗口从取得 PCM 到完成告警前的
`[profile] total` 必须小于 250 ms，且连续运行时处理延迟不能逐步增长。

## 2. 已有基线

在 ESP32-S3 240 MHz、`combined_A_pure.wav`、关闭 OLED、正式性能固件下，已观测到：

| 项目 | 实测值 | 判定 |
| --- | ---: | --- |
| 特征提取 `feature` | 60–70 ms | 正常 |
| 模型阶段 `infer` | 20 ms | 相比五卷积版本的 30–40 ms 继续下降 |
| 单窗口 `total` | 80–90 ms | 小于 250 ms |
| 单窗口余量 | 160–170 ms | 约 64% |
| 文件回放速度 | 约 2.8× 实时 | 无积压 |

这些是文件输入的业务侧毫秒统计，不等同于纯模型 CCOUNT。纯 Invoke 的正式基线见
[`Reference与ESP-NN性能对比操作手册.md`](../项目基线/Reference与ESP-NN性能对比操作手册.md)：六节点组合
平均为 17.591 ms，`output_hash=0x77a10bab`。

## 3. 前置条件

- 固定同一块 ESP32-S3 N16R8 开发板、相同供电、240 MHz CPU 配置和同一串口波特率；
- 使用同一个 LittleFS 资源镜像及 `/data/combined_A_pure.wav`；
- 性能测试使用 `audio_event_espnn_profile`，不得打开 TRACE 或任一 `*_VERIFY`；
- 数值确认使用独立的 `audio_event_espnn_verify`，其耗时不能写入性能结果；
- 运行时关闭 OLED，避免 UI 刷新成为不可控变量：`--no-oled`。

## 4. 阶段 A：数值正确性门禁

每次修改模型、ESP-NN dispatcher、量化参数、Tensor Arena 或 profile 后，先构建验证固件：

```sh
cd ~/openvela
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/audio_event_espnn_verify -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

以真实 WAV 回放一次：

```sh
audio_event --file /data/combined_A_pure.wav --repeat 1 --profile --no-oled
```

验收日志必须显示六个节点为 `backend=esp-nn`，并至少包含各节点的 `match`；Mean 的关键证据为：

```text
[espnn-verify] Mean out_t=28 match bytes=24
```

如果任一节点不匹配，wrapper 会恢复该节点的 reference 输出。此时可继续定位问题，但禁止将该固件的
`infer` 或 `total` 用作加速性能结论。

## 5. 阶段 B：文件回放性能

### 5.1 常规性能记录

构建并烧录正式性能固件：

```sh
cd ~/openvela
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/audio_event_espnn_profile -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

执行一次完整文件回放：

```sh
audio_event --file /data/combined_A_pure.wav --repeat 1 --profile --no-oled
```

`combined_A_pure.wav` 约 5 MiB，在 16 kHz、单声道、PCM16 下约 160 秒，已包含约 640 个 250 ms 窗口；
一次回放足够计算 P50/P95/最大值。`--repeat 1` 表示文件处理一次，**不是只执行一次推理**。

从原始串口日志提取仅实际执行了模型的 `[profile]` 行，统计 `feature`、`infer`、`total` 的 min/P50/mean/P95/max，
并记录：窗口总数、`[power_gate]` 中的 `infer/skip`、`[app] stopped ... status=0` 以及告警次数。

### 5.2 Reference 对照

在同一 WAV、相同参数和同一板卡上，构建 `audio_event_espnn_ref_profile` 并重复 5.1。比较时只比较实际推理窗口：

```text
端到端 infer 加速比 = reference_infer_mean / espnn_infer_mean
端到端 total 降幅 = 1 - espnn_total_mean / reference_total_mean
```

不能把能量门跳过的静音窗口混入模型 `infer` 统计，也不能把 verify 固件的串口打印时间与性能固件相比。

### 5.3 长稳与积压检查

`--repeat 20` 会把完整 WAV 重复 20 次；5 MiB WAV 单次处理约一分钟，因此该命令预计持续约 20 分钟，仅用于长稳测试：

```sh
audio_event --file /data/combined_A_pure.wav --repeat 20 --profile --no-oled
```

观察首段、中段、末段日志：若 `total`、`wall - t` 的趋势持续增长，或最终出现采集/内存/协处理器错误，即判定存在积压或稳定性问题。
对于当前 80–90 ms 的窗口耗时，`wall` 的增长应显著慢于输入音频时间 `t` 的增长。

## 6. 阶段 C：真实麦克风验收

文件回放验证的是确定性吞吐；最终实时性必须用真实 I2S 输入确认：

```sh
audio_event --device /dev/audio/pcm_in1 --profile --no-oled
```

建议连续运行至少 30 分钟，并分别覆盖安静环境、说话背景、目标咳嗽/敲击、距离变化和环境噪声。记录：

- `[profile] total` 是否持续低于 250 ms；
- 是否出现音频采集错误、`Invoke` 失败、重启、Brownout 或协处理器异常；
- 告警类别、触发次数、误报和漏报；
- 若可导出 PCM，再检查削波、静音插入、爆音和底噪，避免把采集质量问题误判为模型问题。

真实设备输入没有文件回放的虚拟时间 `t` 对照，因此应重点观察日志时间戳是否越来越滞后，以及是否发生 buffer overflow/underflow。

## 7. 通过标准

| 项目 | 通过条件 |
| --- | --- |
| 数值正确性 | Conv 23/25/27、DW 24/26、Mean 28 均有 reference `match` 证据；输出类别和概率无异常。 |
| 文件性能 | 实际推理窗口的 `total` P95 < 250 ms，且 `infer` P95 < 250 ms。 |
| 文件长稳 | `--repeat 20` 完成，`status=0`，无重启、错误和处理延迟增长。 |
| 实时采集 | 连续 30 分钟无音频错误、无积压，`total` P95 < 250 ms。 |
| 功能回归 | 与 reference 在相同 WAV 上的类别序列、告警次数及误报/漏报差异可解释且可接受。 |

## 8. 日志归档

原始 picocom 日志保存到 [`logs/测试日志`](../../logs/测试日志)。建议新增命名：

```text
ds_cnn_small_nn5_mean_fileA.log
ds_cnn_small_nn5_mean_fileA_repeat20.log
ds_cnn_small_nn5_mean_mic_<环境>.log
```

其中 `fileA` 表示 `combined_A_pure.wav`。在 [`logs/readme.md`](../../logs/readme.md) 的索引表中补充固件
profile、输入条件、命令、P50/P95、告警统计和通过结论，使比赛报告可以直接追溯原始证据。
