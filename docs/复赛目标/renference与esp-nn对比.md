# s3-large-8class：Reference 与 ESP-NN 性能对比

> 测试对象：ESP32-S3 DevKit，`s3_large_8class` INT8 模型。
> 模型：37,784 B，18,568 参数，输入 `[1,49,40,3]`，输出 8 类。
> 结论状态：Reference 与 ESP-NN 均已完成同口径的 100 次 Invoke 测量；本文只记录这两套
> 性能 profile 的对比，不包含逐节点数值验证 profile。

## 1. 目标与模型能力

模型类别顺序为：

```text
knock, cough, glass_breaking, yes, no, stop, background, silence
```

该模型是事件检测与关键词识别合并的单模型候选。其算子主干为：

```text
[49,40,3]
  -> Conv2D 5x5, stride 2, 3 -> 32
  -> DW 3x3, 32 -> PW 1x1, 32 -> 64
  -> DW 3x3, 64 -> PW 1x1, 64 -> 64
  -> DW 3x3, 64 -> PW 1x1, 64 -> 96
  -> spatial Mean [1,25,20,96] -> [1,96]
  -> FullyConnected 96 -> 8 -> Softmax
```

ESP-NN 选择 4 个 Conv2D、3 个 DepthwiseConv2D 和空间 Mean；整形、FullyConnected、Softmax
继续使用 TFLite Micro reference kernel。

## 2. 对比 profile

| 用途 | defconfig | ESP-NN 状态 | Arena |
| --- | --- | --- | ---: |
| Reference 基线 | `tflm_benchmark_s3_large_ref` | 编入 wrapper，但不选择 Conv/DW/Mean，全部使用 reference | 196,608 B |
| 正式性能 | `tflm_benchmark_s3_large_espnn_cycles` | 选择全部 4 Conv、3 DW、Mean；关闭 TRACE/VERIFY | 196,608 B |

Reference profile 保留 `CONFIG_TFLITEMICRO_ESP_NN=y`，目的是保持 TFLM wrapper 的构建/链接条件；它没有
输出 tensor 选择掩码，运行时并不会调用 ESP-NN。

选择的模型输出 tensor 为：

```text
Conv2D:          27, 29, 31, 33
DepthwiseConv2D: 28, 30, 32
Mean:            34
```

## 3. 测量口径

所有可比较数据均使用同一块 ESP32-S3、240 MHz CCOUNT、固定的板端 `pattern` INT8 输入。

| 命令模式 | 计时对象 | 用途 |
| --- | --- | --- |
| `--mode invoke --warmup 20 --repeat 100` | 一次完整 `MicroInterpreter::Invoke()` | 正式的 min/P50/mean/P95/max 时延 |
| `--mode operator --warmup 10 --repeat 1 --csv` | 每一个算子 | 解释热点与各算子加速比 |

`Invoke` 计时不包括音频采集、特征提取、串口打印、检测器、OLED 或远程上报；它只反映模型阶段。

## 4. 当前整模型 Invoke 结果

### 4.1 Reference 正式性能（100 次）

命令：

```sh
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
```

| 指标 | Reference |
| --- | ---: |
| arena 实际使用 | 85,876 B |
| cycles min | 709,186,753 |
| cycles P50 | 709,192,222 |
| cycles mean | 709,191,831 |
| cycles P95 | 709,197,742 |
| cycles max | 709,199,153 |
| 时间 min | 2,954.944 ms |
| 时间 P50 | 2,954.967 ms |
| 时间 mean | **2,954.965 ms** |
| 时间 P95 | **2,954.990 ms** |
| 时间 max | 2,954.996 ms |
| output hash | `0xc18cf11e` |

### 4.2 ESP-NN 正式性能（100 次）

命令：

```sh
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
```

| 指标 | ESP-NN |
| --- | ---: |
| arena 实际使用 | 132,692 B |
| cycles min | 11,334,998 |
| cycles P50 | 11,346,360 |
| cycles mean | 11,347,697 |
| cycles P95 | 11,353,989 |
| cycles max | 11,354,702 |
| 时间 min | 47.229 ms |
| 时间 P50 | 47.276 ms |
| 时间 mean | **47.282 ms** |
| 时间 P95 | **47.308 ms** |
| 时间 max | 47.311 ms |
| output hash | `0xc18cf11e` |

两侧都完成 100 次采样。Reference 的 P50–P95 为 0.023 ms，ESP-NN 的 P50–P95 为 0.032 ms；
两套固件均有很小的运行抖动。ESP-NN 的 47.282 ms 小于 250 ms hop，模型 Invoke 本身的实时余量约 202.7 ms。

### 4.3 正式整模型对比

| 指标 | Reference | ESP-NN | 结果 |
| --- | ---: | ---: | --- |
| mean cycles | 709,191,831 | 11,347,697 | **62.4965x 加速** |
| P95 cycles | 709,197,742 | 11,353,989 | **62.4624x 加速** |
| mean 时延 | 2,954.965 ms | 47.282 ms | 降低 **98.400%** |
| P95 时延 | 2,954.990 ms | 47.308 ms | 降低 **98.399%** |
| output hash | `0xc18cf11e` | `0xc18cf11e` | 一致 |
| 记录次数 | 100 | 100 | 完成 |

## 5. 算子级对比

两侧均执行：

```sh
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

`Ticks` 单位是 CPU cycles；下表是相同算子计时口径的逐节点比较。

| Event | 算子 | Reference cycles | ESP-NN cycles | 加速比 |
| ---: | --- | ---: | ---: | ---: |
| 0 | Shape | 6,251 | 11,088 | — |
| 1 | StridedSlice | 26,792 | 35,780 | — |
| 2 | Pack | 7,156 | 11,767 | — |
| 3 | Reshape | 8,811 | 11,087 | — |
| 4 | Conv2D：Stem 5x5，3 -> 32 | 104,105,549 | 2,580,610 | **40.3x** |
| 5 | DepthwiseConv2D：32 ch | 15,459,798 | 734,956 | **21.0x** |
| 6 | Conv2D：PW，32 -> 64 | 88,572,672 | 1,278,185 | **69.3x** |
| 7 | DepthwiseConv2D：64 ch | 30,860,642 | 1,337,615 | **23.1x** |
| 8 | Conv2D：PW，64 -> 64 | 170,517,285 | 1,487,709 | **114.6x** |
| 9 | DepthwiseConv2D：64 ch | 30,857,015 | 1,336,324 | **23.1x** |
| 10 | Conv2D：PW，64 -> 96 | 255,764,348 | 2,221,151 | **115.1x** |
| 11 | Mean：25 x 20 x 96 | 13,054,827 | 366,646 | **35.6x** |
| 12 | FullyConnected | 25,050 | 36,281 | — |
| 13 | Softmax | 47,106 | 60,641 | — |
| | **算子合计** | **709,313,302** | **11,509,840** | **61.6x** |

从结构上看，四个 Conv2D 的 Reference 合计为 618,959,854 cycles，占 Reference 算子总量约 87.3%；
ESP-NN 将其降至 7,567,655 cycles，合计加速约 81.8x。三个 DepthwiseConv2D 合计加速约 22.6x，
Mean 加速约 35.6x。

算子 CSV 合计对应的 ESP-NN 时间约 47.96 ms；与 100 次 Invoke mean 的 47.282 ms 接近。二者存在少量差异，
原因是 operator profiler 本身的计时/事件记录开销；正式报告应以 Invoke 100 次统计为准。

## 6. Arena 与资源代价

| 指标 | Reference | ESP-NN 性能版 | 变化 |
| --- | ---: | ---: | ---: |
| 配置 arena | 196,608 B | 196,608 B | 不变 |
| arena 实际使用 | 85,876 B | 132,692 B | +46,816 B |
| 剩余空间 | 110,732 B | 63,916 B | 性能版仍有余量 |
| 模型字节数 | 37,784 B | 37,784 B | 不变 |

ESP-NN 的额外内存来自 Conv/DW 的 scratch、对齐 filter 副本等运行时缓冲。性能 profile 在 192 KiB
arena 下已成功 `AllocateTensors()`，因此当前不需要扩大正式性能 arena。

日志中的：

```text
[model] arena ptr=0x3fc93f80 size=196608 align_256=0
```

说明 arena 起始地址满足 256 字节对齐；这对 ESP-NN 的 SIMD/scratch 使用是必要的前提。

## 7. 复现与日志归档

Reference 与 ESP-NN 性能均使用下列相同命令；仅在烧录的 defconfig 上不同：

```sh
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

建议归档四份完整串口输出：

```text
s3_large_ref_invoke.log
s3_large_ref_operator.csv
s3_large_espnn_invoke.log
s3_large_espnn_operator.csv
```

## 8. 可报告结论

- S3-large-8class 的 Reference Invoke mean 为 **2,954.965 ms**，热点高度集中在四个 Conv2D。
- 全 ESP-NN 性能 profile 的完整模型 Invoke mean 为 **47.282 ms**、P95 为 **47.308 ms**，远低于
  250 ms hop。
- 整模型 mean/P95 的正式加速比分别为 **62.4965x** / **62.4624x**；mean 时延降低 **98.400%**。
- 在同口径的算子 CSV 中，Reference 到 ESP-NN 的总 cycle 降幅为 **61.6x**；最末级 64 -> 96
  pointwise Conv 的单算子加速约 **115.2x**，解释了整体收益来源。
- ESP-NN 额外消耗 46,816 B arena，但 192 KiB 正式配置仍保留 63,916 B 余量。
- 两侧固定输入的 `output_hash` 均为 `0xc18cf11e`，本性能对比的模型输出摘要一致。

## 9. 相关文档

- [S3-large_8class_ESP-NN优化操作手册](../项目基线/S3-large_8class_ESP-NN优化操作手册.md)
- [S3-large_8class Reference 与 ESP-NN 性能对比](../项目基线/S3-large_8class_Reference与ESP-NN性能对比.md)
- [Reference 与 ESP-NN 性能对比操作手册](../项目基线/Reference与ESP-NN性能对比操作手册.md)
- [ESP-NN 移植到 openvela 实施指南](../优化文档/ESP-NN移植到openvela实施指南.md)
