# Reference 与 ESP-NN 性能对比操作手册

本文定义 4-class audio event 模型在 ESP32-S3 上的正式性能对比方法。目标是比较：

- **reference**：全部 `Conv2D` 和 `DepthwiseConv2D` 使用 TFLite Micro reference kernel；
- **ESP-NN 三 Conv2D 组合**：仅 `out_t=23`、`out_t=25`、`out_t=27` 的普通 `Conv2D` 使用
  ESP-NN；两个 `DepthwiseConv2D` 暂保持 reference。

该对比测量的是模型的纯 `Invoke()` 时延，不代表音频采集、特征提取、检测逻辑、显示或串口输出的
端到端时延。

## 为什么使用 ESP-NN

本项目运行在 ESP32-S3（Xtensa LX7），因此选择 Espressif 的 ESP-NN 作为 TFLM 的目标专用 backend：
它覆盖当前 int8 模型最耗时的 `Conv2D` 与 `DepthwiseConv2D`，并提供 ESP32-S3 可执行的优化 C/Xtensa
汇编路径。它不是 ARM 的 CMSIS-NN/CMSIS-DSP，也不是需要 HiFi DSP ISA 的 Xtensa HiFi backend；后两者不适用于
ESP32-S3。

这个选择以实测而非名称判断：全 reference 的 mean `Invoke()` 为 346.271 ms，三个 Conv2D 启用 ESP-NN 后为
82.809 ms，五个卷积节点全部启用后为 30.948 ms，且三组测试的 `output_hash` 均为 `0x77a10bab`。因此本文继续采用
ESP-NN；同时保留 reference profile、固定输入和逐字节 verify profile，确保收益不是由模型、输入、计时边界或数值
变化造成的。

ESP-NN 不是无条件替换：节点必须满足 wrapper 的 int8、batch、形状、对齐和参数约束，并显式列入当前模型的 tensor
白名单。它会增加 scratch（五节点组合 Arena 为 44,324 B，而 reference 为 22,788 B）；更换模型后必须重新做准入和
验证，不能照搬本手册的 tensor ID。

## 1. 测试口径

两套固件均使用 ESP32-S3 `CCOUNT` 计数器。计时边界为：量化 int8 输入已经复制入 input tensor 后，
到 `MicroInterpreter::Invoke()` 返回为止。因此不包含输入准备、输入拷贝、输出打印和统计计算。

| 项目 | 固定条件 |
| --- | --- |
| 板卡 | ESP32-S3 DevKit，使用同一块板进行两次测试 |
| CPU 频率 | 240 MHz |
| 模型 | 4-class audio event int8，模型大小 11,984 B |
| 输入 | 内置、确定性非零 int8 `pattern` |
| 预热/记录 | warmup=20，repeat=100 |
| 测量输出 | cycle、微秒、min/P50/mean/P95/max、`output_hash` |
| ESP-NN TRACE/VERIFY | 必须关闭 |

`pattern` 由程序在板端生成，不依赖随机数或串口数据。两套固件使用相同的输入；`output_hash` 相同是
本次性能比较的数值一致性前提。

## 2. 对比配置

| 名称 | defconfig | 节点选择 |
| --- | --- | --- |
| Reference | `tflm_benchmark_espnn_cycles_ref` | 全部 reference；保留 ESP-NN wrapper 的构建条件，但不选择任何节点 |
| ESP-NN 三 Conv2D | `tflm_benchmark_espnn_cycles` | Conv2D output tensor mask=`0x0a800000`，即 23、25、27 |
| ESP-NN 三 Conv2D + DW26 验证 | `tflm_benchmark_espnn_cycles_dw26_verify` | 三 Conv2D + Depthwise `out_t=26`；开启 TRACE/VERIFY，仅用于正确性 |
| ESP-NN 三 Conv2D + DW26 性能 | `tflm_benchmark_espnn_cycles_dw26` | 三 Conv2D + Depthwise `out_t=26`；关闭 TRACE/VERIFY，用于正式 CCOUNT |
| ESP-NN 三 Conv2D + DW24 验证 | `tflm_benchmark_espnn_cycles_dw24_verify` | 三 Conv2D + Depthwise `out_t=24`；开启 TRACE/VERIFY，仅用于正确性 |
| ESP-NN 三 Conv2D + DW24 性能 | `tflm_benchmark_espnn_cycles_dw24` | 三 Conv2D + Depthwise `out_t=24`；关闭 TRACE/VERIFY，用于正式 CCOUNT |
| ESP-NN 三 Conv2D + DW24/DW26 验证 | `tflm_benchmark_espnn_cycles_dw24_dw26_verify` | 三 Conv2D + 两个 Depthwise；开启 TRACE/VERIFY，仅用于正确性 |
| ESP-NN 三 Conv2D + DW24/DW26 性能 | `tflm_benchmark_espnn_cycles_dw24_dw26` | 三 Conv2D + 两个 Depthwise；关闭 TRACE/VERIFY，用于正式 CCOUNT |

两者均启用 `CONFIG_TFLITEMICRO_ESP32S3_CCOUNT_PROFILER=y` 和
`CONFIG_XTENSA_CP_INITSET=0x0009`。reference 配置保留 ESP-NN wrapper 的编译和链接，避免仅因二进制
布局、编译条件不同而污染对比。

## 3. 测试前检查

1. 使用稳定供电和数据线；先让固件空闲停在 NSH，确认不会自行复位。
2. 启动日志中不得出现 `BROWNOUT_RST`、`SHA-256 comparison failed` 或异常重启。
3. 运行本地建链脚本：

   ```sh
   ./ccf_audioevent/scripts/link_esp_nn.sh
   ```

4. reference 与 ESP-NN 测试尽量在同一温度、同一 USB 供电、同一串口设置下连续完成。

若发生 brownout、启动镜像 SHA 失败、panic、输出 hash 改变或 Invoke 失败，当前轮次无效；先解决
稳定性问题，不得纳入性能统计。

## 4. 采集 Reference 基线

构建并烧录 reference 配置：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_cycles_ref -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

将 `/dev/ttyUSB0` 替换为实际串口。烧录工具应报告数据校验成功；固件进入 NSH 后运行：

```sh
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
```

保存完整串口日志，至少保存以下三行：

```text
[tflm_benchmark] mode=invoke input=pattern warmup=20 repeat=100 ccount_hz=240000000
[tflm_benchmark] invoke_cycles ...
[tflm_benchmark] invoke_us ... output_hash=...
```

### 当前已测 Reference 结果

| 指标 | 结果 |
| --- | ---: |
| Arena 配置/实际使用 | 65,536 B / 22,788 B |
| `invoke_cycles` min | 83,098,131 |
| `invoke_cycles` P50 | 83,099,802 |
| `invoke_cycles` mean | 83,105,174 |
| `invoke_cycles` P95 | 83,111,918 |
| `invoke_cycles` max | 83,112,068 |
| `invoke_us` mean | 346,271 us（346.271 ms） |
| `invoke_us` P95 | 346,299 us（346.299 ms） |
| `output_hash` | `0x77a10bab` |

这 100 次记录的 min–max 范围为 13,937 cycles，约 58 us，说明这次 reference 基线具有足够的
稳定性，可作为 ESP-NN 对照。

## 5. 采集 ESP-NN 三 Conv2D 结果

构建并烧录 ESP-NN 配置：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_cycles -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

进入 NSH 后执行完全相同的命令：

```sh
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
```

先检查：

```text
ccount_hz=240000000
output_hash=0x77a10bab
```

两项任何一项不符合，都不要计算加速比。通过后，将结果填写到下一节的表格。

### 当前已测 ESP-NN 三 Conv2D 结果

| 指标 | 结果 |
| --- | ---: |
| Arena 配置/实际使用 | 65,536 B / 32,324 B |
| `invoke_cycles` min | 19,864,192 |
| `invoke_cycles` P50 | 19,873,933 |
| `invoke_cycles` mean | 19,874,378 |
| `invoke_cycles` P95 | 19,878,193 |
| `invoke_cycles` max | 19,879,045 |
| `invoke_us` mean | 82,809 us（82.809 ms） |
| `invoke_us` P95 | 82,825 us（82.825 ms） |
| `output_hash` | `0x77a10bab` |

100 次记录均完成，min–max 范围为 14,853 cycles，约 62 us。它与 reference 的
`output_hash=0x77a10bab` 相同，满足本次三 Conv2D ESP-NN 组合的数值一致性要求。

## 6. 正式结果表与计算

| 指标 | Reference | ESP-NN 三 Conv2D | 计算/判定 |
| --- | ---: | ---: | --- |
| mean cycles | 83,105,174 | 19,874,378 | **4.1815× 加速** |
| P95 cycles | 83,111,918 | 19,878,193 | **4.1811× 加速** |
| mean us | 346,271 | 82,809 | 时延降低 **76.085%** |
| P95 us | 346,299 | 82,825 | 时延降低约 76.1% |
| output hash | `0x77a10bab` | `0x77a10bab` | 一致 |
| 稳定性 | 100/100 成功 | 100/100 成功 | 通过 |
| Arena 实际使用 | 22,788 B | 32,324 B | ESP-NN scratch 增加 9,536 B |

计算公式：

```text
mean_speedup       = 83,105,174 / espnn_mean_cycles
mean_latency_drop  = (1 - espnn_mean_cycles / 83,105,174) × 100%
p95_speedup        = 83,111,918 / espnn_p95_cycles
```

本轮计算结果为 mean **4.1815×**、P95 **4.1811×**；模型纯 Invoke mean 从 346.271 ms 降至
82.809 ms。报告中应同时给出 mean 与 P95。不能只报单次最小值，也不能用旧的 10 ms
`MicroProfiler` 刻度计算正式加速比。

### 已测组合总览

下表统一采用 ESP32-S3 240 MHz、`pattern` 输入、warmup=20、repeat=100 的独立 Invoke CCOUNT
结果。DW24/DW26 的两行是保留的单 Depthwise 回归 profile；最后一行才是用 mask `0x05000000`
同时启用两个 Depthwise 的实际组合测量，不能由前两行相加或推算得到。

| 配置 | 实际 ESP-NN 节点（output tensor） | mean cycles | mean 时延 | P95 时延 | Arena 使用 | output hash | 相对 reference mean 加速 |
| --- | --- | ---: | ---: | ---: | ---: | --- | ---: |
| 全 reference | 无 | 83,105,174 | 346.271 ms | 346.299 ms | 22,788 B | `0x77a10bab` | 1.0000× |
| 三 Conv2D | 23、25、27 | 19,874,378 | 82.809 ms | 82.825 ms | 32,324 B | `0x77a10bab` | 4.1815× |
| 三 Conv2D + DW26 | 23、25、26、27 | 12,570,446 | 52.376 ms | 52.415 ms | 32,324 B | `0x77a10bab` | 6.6112× |
| 三 Conv2D + DW24 | 23、24、25、27 | 14,759,659 | 61.498 ms | 61.512 ms | 44,324 B | `0x77a10bab` | 5.6306× |
| 三 Conv2D + DW24 + DW26 | 23、24、25、26、27 | 7,427,604 | 30.948 ms | 30.973 ms | 44,324 B | `0x77a10bab` | **11.1887×** |

五节点组合将纯模型 Invoke 相对 reference 降低 **91.062%**，相对三 Conv2D 基线再降低
**62.627%**。其 44,324 B Arena 仍距 65,536 B 上限余 21,212 B。数值正确性的最终归档条件仍是
组合 verify profile 在同一次执行中同时输出 DW24、DW26 的 `match` 日志。

## 7. 每算子 CCOUNT 采集

整次 Invoke 通过后，可进一步解释收益来源。分别在两套固件上运行：

```sh
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

CSV 中的 `Ticks` 单位是 **CPU cycle**。保存原始 CSV；若要得到每个算子的 mean/P95，应在每套固件
上重复采集足够轮次，并按 Event 序号与 Tag 聚合。重点对比：

| Event | Reference backend | ESP-NN backend | 预期 |
| --- | --- | --- | --- |
| Conv2D `out_t=23` | reference | ESP-NN | 明显降 cycle |
| DepthwiseConv2D `out_t=24` | reference | reference | 接近 |
| Conv2D `out_t=25` | reference | ESP-NN | 明显降 cycle |
| DepthwiseConv2D `out_t=26` | reference | reference | 接近 |
| Conv2D `out_t=27` | reference | ESP-NN | 明显降 cycle |

每算子 cycle 只能说明热点变化；整模型正式时延以第 4、5 节的独立 `--mode invoke` 统计为准。

### 当前 ESP-NN 每算子快照

以下数据来自 ESP-NN 三 Conv2D 固件的 `--mode operator --warmup 10 --repeat 1 --csv`。operator
模式当前使用 float-zero 输入；它用于观察算子工作量，而第 6 节的非零 `pattern` CCOUNT 结果才是
正式总时延结论。

| Event | 算子/节点 | cycle | 换算时延 | backend |
| ---: | --- | ---: | ---: | --- |
| 0 | Shape | 5,839 | 0.024 ms | reference |
| 1 | StridedSlice | 33,660 | 0.140 ms | reference |
| 2 | Pack | 8,560 | 0.036 ms | reference |
| 3 | Reshape | 9,375 | 0.039 ms | reference |
| 4 | Conv2D `out_t=23` | 1,187,309 | 4.947 ms | ESP-NN |
| 5 | DepthwiseConv2D `out_t=24` | 5,840,209 | 24.334 ms | reference |
| 6 | Conv2D `out_t=25` | 1,307,738 | 5.449 ms | ESP-NN |
| 7 | DepthwiseConv2D `out_t=26` | 7,739,194 | 32.247 ms | reference |
| 8 | Conv2D `out_t=27` | 461,141 | 1.921 ms | ESP-NN |
| 9 | Mean | 3,301,122 | 13.755 ms | reference |
| 10 | FullyConnected | 16,801 | 0.070 ms | reference |
| 11 | Softmax | 48,089 | 0.200 ms | reference |

该次所有事件的合计为 19,959,037 cycles（83.163 ms）。它比 pattern 输入下的独立 Invoke mean
82.809 ms 高约 0.43%，这是 profiler 边界与输入形式不同带来的预期差异，不能代替第 6 节的总
Invoke 统计。两个 DepthwiseConv2D 合计 13,579,403 cycles（56.581 ms），占该快照约 68.04%，
是当前最主要的优化候选；三个 ESP-NN Conv2D 合计仅 2,956,188 cycles（12.317 ms）。

## 8. DW26 组合验证与性能测试

`out_t=26` 是 `[1,25,20,16]` 的 3×3、stride=1、SAME DepthwiseConv2D，满足当前 wrapper 的
16-channel 白名单。先构建验证 profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_cycles_dw26_verify -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
tflm_benchmark --mode invoke --input pattern --warmup 0 --repeat 1
```

通过条件是启动日志显示 `DepthwiseConv2D out_t=26` 的 backend 为 `esp-nn`，并出现
`[espnn-verify] DepthwiseConv2D out_t=26 match bytes=8000`；输出 hash 仍必须是
`0x77a10bab`。验证固件的 TRACE/VERIFY 会改变 Arena 占用与耗时，不能用于性能结论。

随后构建无 TRACE/VERIFY 的性能 profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_cycles_dw26 -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

性能 profile 的 `output_hash` 必须为 `0x77a10bab`，并与三 Conv2D 基线的 19,874,378 mean cycles
比较。

### 当前已测 DW26 组合结果

DW26 性能 profile 完成 100/100 次 pattern Invoke，`output_hash=0x77a10bab`。结果如下：

| 指标 | 三 Conv2D | 三 Conv2D + DW26 | 对比 |
| --- | ---: | ---: | --- |
| mean cycles | 19,874,378 | 12,570,446 | 1.5810× 加速 |
| P95 cycles | 19,878,193 | 12,579,667 | 1.5802× 加速 |
| mean 时延 | 82.809 ms | 52.376 ms | 降低 36.750% |
| P95 时延 | 82.825 ms | 52.415 ms | 降低约 36.7% |
| Arena 实际使用 | 32,324 B | 32,324 B | 峰值未增加 |

相对 reference 的 346.271 ms mean，三 Conv2D + DW26 达到 6.6112× 总加速，时延降低约 84.87%。
单次 operator 快照中，DW26 从 7,739,194 cycles（32.247 ms）降至 386,486 cycles（1.610 ms），
约 20.02× 加速。DW24 和 Mean 现在是主要热点。

## 9. DW24 组合验证与性能测试

`out_t=24` 是 `[1,25,20,12]` 的 3×3、stride=1、SAME DepthwiseConv2D。现有 wrapper 对它使用
12→16 通道补齐的 s16 兼容路径。DW24 profile 不选择 DW26，因此能够将 DW24 的收益和风险与三
Conv2D 基线独立比较。

先在不改变 65,536 B Arena 配置的条件下构建验证 profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_cycles_dw24_verify -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
tflm_benchmark --mode invoke --input pattern --warmup 0 --repeat 1
```

通过条件为 `DepthwiseConv2D out_t=24` 的 backend 为 `esp-nn`，并出现
`[espnn-verify] DepthwiseConv2D out_t=24 match bytes=6000`，最终 `output_hash` 仍为
`0x77a10bab`。若 `AllocateTensors()` 失败，应记录实际 Arena 需求；不要在验证前预先提高 Arena，
以免掩盖该路径的真实内存成本。

验证通过后，再使用性能 profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_cycles_dw24 -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

将结果与三 Conv2D 基线的 19,874,378 mean cycles / 82.809 ms 比较。只有输出 hash 一致、100/100
稳定、Arena 有余量且 Event 5 低于约 24.322 ms，DW24 才具备加入后续双 Depthwise 组合的资格。

### 当前已测 DW24 组合结果

DW24 verify profile 已打印 `match bytes=6000`；性能 profile 在 `pattern` 输入下完成 100/100 次
Invoke，最终 `output_hash=0x77a10bab`。Arena 使用 44,324 B，距 65,536 B 上限还余 21,212 B。

| 指标 | 三 Conv2D | 三 Conv2D + DW24 | 对比 |
| --- | ---: | ---: | --- |
| mean cycles | 19,874,378 | 14,759,659 | 1.3465× 加速 |
| P95 cycles | 19,878,193 | 14,762,974 | 1.3465× 加速 |
| mean 时延 | 82.809 ms | 61.498 ms | 降低 25.735% |
| P95 时延 | 82.825 ms | 61.512 ms | 降低约 25.7% |
| Arena 实际使用 | 32,324 B | 44,324 B | 增加 12,000 B |

相对全 reference 的 346.271 ms mean，该组合达到 5.6306× 总加速。单次 operator 快照中，DW24
从 5,840,209 cycles（24.334 ms）降至 659,157 cycles（2.746 ms），约 8.86× 加速；此 profile
中的 DW26 仍为 reference（7,766,015 cycles）。因此 DW24 已满足独立节点的正确性、稳定性和性能
准入条件，但还没有“三 Conv2D + 两个 Depthwise”同时启用的正式结果。

## 10. DW24 + DW26 组合验证与性能测试

该组合通过 Depthwise output tensor mask `0x05000000` 同时选择 bit 24 与 bit 26；它与保留的单 ID
配置是“或”关系，组合 profile 不设置单 ID，因此不会额外选择其他节点。先构建验证 profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_cycles_dw24_dw26_verify -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
tflm_benchmark --mode invoke --input pattern --warmup 0 --repeat 1
```

必须同时看到以下两条逐字节校验，并确认最终 `output_hash=0x77a10bab`：

```text
[espnn-verify] DepthwiseConv2D out_t=24 match bytes=6000
[espnn-verify] DepthwiseConv2D out_t=26 match bytes=8000
```

验证成功且 `AllocateTensors()` 的 Arena 使用未超过 65,536 B 后，再构建无 TRACE/VERIFY 的性能
profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_cycles_dw24_dw26 -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

归档 mean/P95 cycles、mean/P95 时延、Arena 实际使用和 output hash，并确认 operator CSV 的 Event 5
与 Event 7 均为 ESP-NN。该数据才是五个卷积节点同时加速的正式结论，不能由 DW24/DW26 两个独立
profile 的结果相加或推算。

### 当前已测组合性能结果

组合性能 profile 已在 `pattern` 输入、warmup=20、repeat=100 下完成 100/100 次 Invoke：

| 指标 | 三 Conv2D + DW24 + DW26 | 对全 reference | 对三 Conv2D |
| --- | ---: | ---: | ---: |
| mean cycles | 7,427,604 | 11.1887× 加速 | 2.6757× 加速 |
| P95 cycles | 7,433,689 | - | - |
| mean 时延 | 30.948 ms | 降低 91.062% | 降低 62.627% |
| P95 时延 | 30.973 ms | - | - |
| Arena 实际使用 | 44,324 B | 距 65,536 B 上限余 21,212 B | 与 DW24 单节点组合相同 |
| output hash | `0x77a10bab` | 与 reference 一致 | 与三 Conv2D 基线一致 |

单次 operator 快照中，Event 5（DW24）为 667,209 cycles（2.780 ms），Event 7（DW26）为
374,649 cycles（1.561 ms）；相对各自的 reference 快照约为 8.75× 与 20.66× 加速。所有事件合计
7,511,898 cycles（31.300 ms），与独立 Invoke mean 相差约 1.1%，属于 profiler 边界和输入形式
带来的预期差异。

以上已经是五个卷积节点同固件、同次 Invoke 的正式性能数据；但当前归档中尚未包含组合 verify 的
串口原始日志。因此“逐字节组合验证通过”仍须以本节前述的 DW24、DW26 两条 `match` 日志为准，取得
日志后再将本组合标记为完整的数值验证通过。

## 11. 归档清单

每次正式对比应一并保存：

- 两个 defconfig 的 Git commit；
- apps 与 ccf_audioevent 的 Git commit；
- 模型 SHA-256 和固件 SHA-256；
- reference、ESP-NN 的完整串口原始日志；
- 输出 hash、mean/P95、加速比和稳定性结论；
- 若结果异常，保存失败日志，不以手工挑选的成功样本替代。

相关实现和 ESP-NN 节点准入过程参见
[ESP-NN 移植到 openvela 实施指南](../优化文档/ESP-NN移植到openvela实施指南.md)。
