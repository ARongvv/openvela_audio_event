# tflm_benchmark 算子剖析

## 用途与边界

`tflm_benchmark` 是针对 M001 的独立内建应用。它复用生产 `audio_event` 的模型数据、
9 个算子 resolver 和 Tensor Arena 分配逻辑，但不链接音频采集、特征提取、能量门控、
检测器或 OLED。因此其日志只用于分析 `MicroInterpreter::Invoke()` 中的图节点开销。

常规 `audio_event` 已不提供 `--tflm-profile` 参数，继续只负责真实音频事件检测。
端到端基线应使用常规固件和命令：

```sh
nsh> cd /data/audio
nsh> audio_event --file combined_A_pure.wav --no-oled --profile
```

## 独立配置

使用 `board/esp32s3-devkit/configs/tflm_benchmark/defconfig`：

```text
CONFIG_EXAMPLES_TFLM_BENCHMARK=y
CONFIG_EXAMPLES_TFLM_BENCHMARK_ARENA_SIZE=65536
CONFIG_TFLITEMICRO_DEBUG=y
```

它不启用 `CONFIG_EXAMPLES_AUDIO_EVENT`，避免业务应用和基准应用同时编译共享的分类器实现。
`CONFIG_TFLITEMICRO_DEBUG` 会启用微秒 tick 和保留算子标签；它会改变调试固件体积，不能
替代无 debug 的端到端性能测试。

## 历史算子基线（旧版通用 benchmark）

在专用 app 落地前，曾以修改公共 TFLite Micro
`generic_model_benchmark.cc` 的方式，对同一 M001 模型完成一轮真机测试。原始日志为
`/home/arongw/openvela/tflm_test.log`，测试参数为 `warmup=5`、`repeat=30`、Arena 为
65,536 B。该实现已撤回，以下结果仅作为历史基线，不与后续专用 app 的结果直接排名。

| 指标 | 历史结果 |
| --- | ---: |
| 单次 Invoke 均值 | 347.7 ms |
| P50 / P95 | 350 / 350 ms |
| 最小 / 最大 | 340 / 350 ms |
| 调试路径 Arena 已用/配置 | 24,836 / 65,536 B（37.9%） |
| 一次性 `AllocateTensors()` | 160 ms（不计入单次 Invoke） |

每轮图中有 12 个节点，但只包含 9 类算子；其中普通卷积出现 3 次，深度可分离卷积出现
2 次。按同类节点在每轮的累计时间汇总如下：

| 算子类别 | 节点数 | 均值 | P50 / P95 | 占 Invoke 均值 |
| --- | ---: | ---: | ---: | ---: |
| `CONV_2D` | 3 | 276.3 ms | 275 / 290 ms | 79.5% |
| `DEPTHWISE_CONV_2D` | 2 | 56.0 ms | 55 / 70 ms | 16.1% |
| `MEAN` | 1 | 14.3 ms | 10 / 20 ms | 4.1% |
| 其余（`Shape`、`Pack`、`Reshape`、`FullyConnected`、`Softmax`、`StridedSlice`） | 6 | 约 1.0 ms | 不可靠 | 约 0.3% |
| **合计** | **12** | **347.7 ms** | **350 / 350 ms** | **100%** |

该结果与常规业务日志的模型阶段均值 349.6 ms 相符，因此足以确认优化优先级为
`Conv2D`，其次为 `DepthwiseConv2D`；Arena 容量不是当前瓶颈。

### 历史结果的计时限制

旧版使用 `clock_gettime(CLOCK_MONOTONIC)`，对应构建的
`CONFIG_USEC_PER_TICK=10000`。日志中所有非零时间也均为 10,000 µs 的整数倍，因此它的
有效分辨率约为 10 ms：`FULLY_CONNECTED=0` 或 `SOFTMAX=0` 表示其耗时低于可分辨粒度，
并不代表完全没有开销。30 轮结果仅为 340 或 350 ms，也主要反映了该量化误差。

此外，旧通用 benchmark 每轮填入不同随机输入，所以日志中的 `Input CRC32` 与
`Output CRC32` 持续变化是预期行为；它只能证明模型持续可运行，不能用于分类准确率评估。
`RecordingMicroInterpreter` 的调试分配记录也会使其 24,836 B 与生产分类器日志中的
22,708 B 不完全相同，生产 Arena 基准仍以后者为准。

## 构建与板端命令

在 openvela 根目录构建：

```sh
./build.sh vendor/espressif/boards/esp32s3/esp32s3-devkit/configs/tflm_benchmark/ -j8
```

烧录后执行：

```sh
nsh> tflm_benchmark --warmup 5 --repeat 30 --csv
```

该命令以固定全零 float 特征调用模型。前 5 次 Invoke 只用于预热，后续 30 次每次输出
一组 CSV。当前 TFLM 计时接口将 tick 定义为微秒：

```text
[tflm_benchmark] iteration=1
"Event","Tag","Ticks"
0,Conv2D,12345
```

卷积等算子运行时间不依赖此模型的具体输入值；固定输入使测量可复现，但不用于评估分类
准确率。省略 `--csv` 时，固件输出逐算子的可读文本日志。

## 主机汇总

保存完整串口日志后执行：

```sh
python3 ccf_audioevent/scripts/summarize_tflm_benchmark.py \
  logs/tflm_benchmark_m001.log
```

脚本输出每个 tag 的 `mean_us`、`p50_us`、`p95_us` 和 `mean_share_pct`。同类 tag 会合并，
因此 `Conv2D` 表示全部普通卷积节点的累计时间，`DepthwiseConv2D` 同理。

注意：若新固件仍使用 `CONFIG_USEC_PER_TICK=10000`，CSV 数值会以 10 ms 为粒度量化。
它适合复核算子占比和发现数十毫秒级回归；若要判断 1--5 ms 优化收益，应先为专用 app
接入更高分辨率的 ESP32-S3 单调计时源。

将原始日志、脚本输出、模型 SHA-256、固件 SHA-256 与无 debug 的端到端结果一同登记到
[ESP32-S3 推理时间优化方案](../优化文档/ESP32-S3推理时间优化方案.md)。
