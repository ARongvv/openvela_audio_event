# S3-large 8-class Reference 与 ESP-NN 性能对比

本文档对比 **s3-large-8class** 模型（37,784 B）在 ESP32-S3 上使用 TFLite Micro reference 内核
与 ESP-NN 优化内核的纯 `Invoke()` 性能。它是
[Reference 与 ESP-NN 性能对比操作手册](Reference与ESP-NN性能对比操作手册.md)（4-class）的 8-class
扩展，方法与口径保持一致。

## 1. 背景与结论摘要

当前 4-class 模型（11,984 B）的正式对比结论为：全 reference 346.271 ms → 全 ESP-NN
（五卷积 + Mean）17.591 ms，约 **19.68×** 加速。

s3-large-8class 是复赛 A（类别扩展）+ F（关键词识别）的单模型候选：8 类 =
`knock/cough/glass_breaking + yes/no/stop + background/silence`。8-class 模型显著更大
（37,784 B，96 通道 PW），因此需要独立的基准确认其 ESP-NN 收益是否足以支持 250 ms hop。

**当前结论（初步数据）：**

| 配置 | Invoke | 相对 reference | 相对 4-class 全 ESP-NN |
| --- | ---: | ---: | ---: |
| reference | 2,957.042 ms | 1.00× | — |
| ESP-NN（4 Conv + 3 DW + Mean）| 48.481 ms | **61.0×** | 2.76×（慢）|

ESP-NN 将 8-class 模型的纯 Invoke 从约 2.96 s 降至约 **48 ms**，满足 250 ms hop（RTF ≈ 0.19）。
该收益来自 7 个卷积节点 + 特化 Mean 的 ESP-NN 替换；四个非卷积整形算子（Shape/StridedSlice/
Pack/Reshape）无加速。

> **状态标注**：本文"当前已测"数据来自单次 `--mode invoke --input pattern --warmup 0 --repeat 1`
> 的初步日志。正式归档需按第 3 节流程完成 ref → verify → cycles 三阶段，并以
> `--warmup 20 --repeat 100` 统计 P50/mean/P95。

## 2. 测试口径

与 4-class 手册一致：

| 项目 | 固定条件 |
| --- | --- |
| 板卡 | ESP32-S3 DevKit，同一块板完成两组测试 |
| CPU 频率 | 240 MHz |
| 模型 | s3-large-8class int8，37,784 B |
| 输入 | 内置、确定性非零 int8 `pattern` |
| 预热/记录 | 初步：warmup=0 repeat=1；正式：warmup=20 repeat=100 |
| 计时边界 | 量化输入已复制入 input tensor 后 → `Invoke()` 返回 |
| 测量输出 | cycle、微秒、min/P50/mean/P95/max、`output_hash` |
| ESP-NN TRACE/VERIFY | 正式性能 profile 必须关闭 |

数值一致性前提：两组固件的 `output_hash` 必须相同。当前两轮均为 `0xc18cf11e` ✓

## 3. 实施流程（三阶段）

三阶段方法确保"先验证正确性，再测性能"：

| 阶段 | defconfig | 用途 |
| --- | --- | --- |
| 1. Reference 基线 | `tflm_benchmark_s3_large_ref` | 全 reference 内核，Arena 192 KiB |
| 2. ESP-NN verify | `tflm_benchmark_s3_large_espnn_verify` | 逐节点字节级 `match` 验证，Arena 256 KiB |
| 3. ESP-NN cycles | `tflm_benchmark_s3_large_espnn_cycles` | 正式性能统计，Arena 192 KiB |

ESP-NN 节点选择范围（output tensor id）：

```text
Conv2D: 27、29、31、33
DepthwiseConv2D: 28、30、32
Mean: 34
```

### 阶段 1：Reference 基线

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_s3_large_ref -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

```sh
tflm_benchmark --mode invoke --input pattern --warmup 0 --repeat 1
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

### 阶段 2：ESP-NN 逐字节验证

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_s3_large_espnn_verify -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
tflm_benchmark --mode invoke --input pattern --warmup 0 --repeat 1
```

通过条件：四个 Conv、三个 DW、Mean 全部打印 `match`：

```text
[espnn-verify] Conv2D out_t=33 match bytes=48000
[espnn-verify] Mean out_t=34 match bytes=96
```

### 阶段 3：ESP-NN 正式性能

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_s3_large_espnn_cycles -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

```sh
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

## 4. Reference 基线（初步数据）

来源：`tflm_benchmark --mode invoke --input pattern --warmup 0 --repeat 1`
（reference defconfig，Arena 192 KiB）。

| 指标 | 值 |
| --- | ---: |
| Arena 配置 / 实际使用 | 196,608 B / 85,876 B |
| `invoke_cycles` | 709,690,304 |
| `invoke_us` | 2,957,042 µs（2,957.042 ms）|
| `output_hash` | `0xc18cf11e` |

## 5. ESP-NN 结果（初步数据）

来源：`tflm_benchmark --mode invoke --input pattern --warmup 0 --repeat 1`
（ESP-NN cycles defconfig，Arena 192 KiB）。

| 指标 | 值 |
| --- | ---: |
| Arena 配置 / 实际使用 | 196,608 B / 132,692 B |
| `invoke_cycles` | 11,635,581 |
| `invoke_us` | 48,481 µs（48.481 ms）|
| `output_hash` | `0xc18cf11e` |

## 6. 正式结果表

| 指标 | Reference | ESP-NN | 计算/判定 |
| --- | ---: | ---: | ---: |
| invoke cycles | 709,690,304 | 11,635,581 | **61.0× 加速** |
| invoke us | 2,957,042 | 48,481 | 时延降低 **98.36%** |
| output hash | `0xc18cf11e` | `0xc18cf11e` | 一致 ✓ |
| Arena 实际使用 | 85,876 B | 132,692 B | ESP-NN scratch 增加 46,816 B |
| Arena 余量 | 110,732 B | 63,916 B | 均未溢出 |

> 正式归档时以 warmup=20 repeat=100 的 mean/P95 替换上表；本节当前为初步单次数据。

## 7. 算子级对比

两组固件的 `--mode operator --warmup 10 --repeat 1 --csv`（Ticks 单位 = CPU cycle）：

| Event | 算子 | reference cycles | ESP-NN cycles | 加速比 | 说明 |
| ---: | --- | ---: | ---: | ---: | --- |
| 0 | Shape | 5,823 | 9,683 | — | 无加速，整形算子 |
| 1 | StridedSlice | 27,008 | 39,162 | — | 无加速 |
| 2 | Pack | 7,001 | 11,600 | — | 无加速 |
| 3 | Reshape | 8,997 | 10,464 | — | 无加速 |
| 4 | **Conv2D**（Stem 5×5，`out_t=27`）| 104,105,310 | 2,573,678 | **40.5×** | ESP-NN |
| 5 | **DepthwiseConv2D**（`out_t=28`）| 15,466,048 | 738,427 | **20.9×** | ESP-NN |
| 6 | **Conv2D**（DS1 PW，`out_t=29`）| 88,572,193 | 1,281,875 | **69.1×** | ESP-NN |
| 7 | **DepthwiseConv2D**（`out_t=30`）| 30,860,432 | 1,336,621 | **23.1×** | ESP-NN |
| 8 | **Conv2D**（DS2 PW，`out_t=31`）| 170,517,070 | 1,487,713 | **114.6×** | ESP-NN |
| 9 | **DepthwiseConv2D**（`out_t=32`）| 30,857,013 | 1,335,528 | **23.1×** | ESP-NN |
| 10 | **Conv2D**（最终 PW 96ch，`out_t=33`）| 255,763,821 | 2,221,152 | **115.1×** | ESP-NN |
| 11 | **Mean**（`out_t=34`）| 13,053,270 | 366,837 | **35.6×** | ESP-NN |
| 12 | FullyConnected | 22,738 | 39,026 | — | 无加速 |
| 13 | Softmax | 48,571 | 62,910 | — | 无加速 |
| | **合计** | **709,315,295** | **11,514,676** | **61.6×** | 与 invoke 统计基本吻合 |

## 8. 结论与热点分析

### 8.1 加速比结构与理论预期一致

| 算子类型 | 加速比 | 原因 |
| --- | ---: | --- |
| 1×1 PW 卷积 | 69–115× | 本质为矩阵乘，SIMD `dot_s8` 点积复用率最高 |
| 5×5 Stem 卷积 | 40.5× | 核内数据搬运占比高，SIMD 收益被访存摊薄 |
| 3×3 DW 卷积 | 21–23× | 每通道独立、无跨通道复用，访存密集型 |
| Mean | 35.6× | 有 esp-nn 特化实现 |
| FC/Softmax/整形 | ~1× | 无 esp-nn 覆盖，但合计仅占 0.4% |

### 8.2 关键数据

- 三个 DW 合计仅 3,410,576 cycles（3.8% 参考值），是**加速比最薄弱的环节**；若未来继续压缩推理
  时间，DW 路径是主要优化候选
- 两个大 PW（Event 8、10）合计 426,280,891 cycles（60.1% 参考值），加速后仅 3,708,865 cycles，
  是**绝对收益最大的环节**
- ESP-NN scratch 使 Arena 使用从 85,876 B 增至 132,692 B（+54%），但 192 KiB 配置下仍余 63,916 B

### 8.3 与 4-class 对比

| 模型 | 全 ESP-NN Invoke | Arena 使用 |
| --- | ---: | ---: |
| 4-class（11,984 B）| 17.591 ms | 44,356 B |
| s3-large-8class（37,784 B）| 48.481 ms | 132,692 B |
| 差距 | +30.9 ms（2.76×）| +88,336 B |

8-class 模型以 2.76× 的推理开销换取 3 事件 + 3 关键词的完整能力，48 ms 仍远低于 250 ms hop
（RTF ≈ 0.19），单模型架构实时可用。

## 9. 已知限制与待办

1. **当前数据为初步单次结果**：须完成三阶段流程，以 warmup=20 repeat=100 的 mean/P95 归档；
2. **verify 尚未执行**：`match bytes` 日志是 ESP-NN 正确性的唯一证据，取得前 48.481 ms 不能作为
   正式归档；
3. **Arena 需在 verify 版确认**：verify profile 用 256 KiB；若 192 KiB 下 `AllocateTensors failed`
   或链接 DRAM/.bss 溢出，需增大 arena 或改从 SPIRAM common heap 做 16 字节对齐分配；
4. **检测器 8 类化**：`event_detector.c` 当前硬编码 knock/cough 阈值，8 类需区分事件类
   （knock/cough/glass）与关键词类（yes/no/stop）的告警语义。

## 10. 相关文档

- [S3-large_8class_ESP-NN优化操作手册](S3-large_8class_ESP-NN优化操作手册.md)：三阶段操作流程
- [Reference 与 ESP-NN 性能对比操作手册](Reference与ESP-NN性能对比操作手册.md)：4-class 对比方法
- [ESP-NN 移植到 openvela 复刻手册](ESP-NN移植到openvela复刻手册.md)：移植与节点准入
