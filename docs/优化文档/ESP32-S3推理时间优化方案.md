# ESP32-S3 推理时间优化方案

## 1. 目的与结论

本文档针对当前 `ccf_audioevent` 的 ESP32-S3/NuttX 固件，记录推理链路的真实状态、
不适用的配置项，以及以实测数据为准的优化顺序。它不是 ESP-IDF 工程的配置说明。

历史 `M001-small-int8` reference 基线的模型阶段约 349.6 ms，超过 250 ms hop，曾是持续 4 Hz
检测无法实时完成的首要原因。该基线用于说明问题，不代表当前 ESP-NN 性能。

当前已用 ESP32-S3 `CCOUNT`（240 MHz、固定 pattern、warmup=20、repeat=100）完成复测：五个卷积节点
使用 ESP-NN 后，纯 `Invoke()` mean 从 346.271 ms 降至 30.948 ms（11.1887×）；在此基础上特化 ESP-NN
Mean 后进一步降至 17.591 ms（19.6840×），`output_hash` 与 reference 一致，并已取得
`[espnn-verify] Mean out_t=28 match bytes=24`。
真实文件播放的应用 profile 中，特征提取约 60--70 ms、模型阶段约 30--40 ms、总窗口处理约 100--110 ms，
低于 250 ms hop；应用端尚未用 Mean 新 profile 重测，因此不将 17.591 ms 直接外推为真实文件 infer。不得以启用
CMSIS-NN、HiFi 或 ARM CMSIS-DSP 替代该路径；它们不适用于 ESP32-S3。

## 2. 当前实现的事实核对

| 项目 | 当前实际状态 | 判断与处理 |
| --- | --- | --- |
| ESP-NN | 已作为受控的 ESP32-S3 TFLM backend 接入 | 选择已验证的 Conv2D/DW 节点；不支持或未选择的节点回退 reference |
| CMSIS-NN | 未启用 | ARM 库，不适用于 Xtensa LX7；`TFLITE_ENABLE_CMSIS_NN=ON` 不是本工程配置 |
| 推理精度 | **全 int8** 输入/输出与 int8 权重 | 已正确量化；特征前端仍以 float 计算，推理前量化 5,880 个元素 |
| FFT | KissFFT RFFT | 当前可用；CMSIS-DSP RFFT 是 ARM 实现，不能直接替换 |
| Tensor Arena | 64 KiB 全局静态数组，初始化一次 | 已复用；实测仅使用 22,708 B，非每次推理重新创建 |
| TFLM 编译优化 | TFLM 静态库使用 `-O3` | 已启用；不能推断为 `-O0/-Og` |
| `-mcpu=xtensa-lx7` | 当前未单独在 TFLM CMake 中设置 | 应以实际编译命令验证 toolchain 默认目标后再评估，不应盲加 |
| `-mfix-esp32-psram-cache-issue` | 当前 ESP32-S3 配置未使用 | 这是旧 ESP32 的 PSRAM cache erratum workaround，不是 SIMD 开关，不应用作推理加速手段 |
| Xtensa HiFi | 未启用 | HiFi3/4/5 DSP 专用，ESP32-S3 LX7 不可开启 |

依据：模型的 `TfLiteTensor` 输入、输出均被强制检查为 `kTfLiteInt8`，算子与静态
Arena 在 [event_classifier.cc](../../app/audio_event/model/event_classifier.cc)；TFLM 的
`-O3`、HiFi/CMSIS-NN backend 选择在
[apps/mlearning/tflite-micro/CMakeLists.txt](../../../apps/mlearning/tflite-micro/CMakeLists.txt)。

## 3. 平台边界

### 3.1 CMSIS-NN 与 CMSIS-DSP

CMSIS-NN 和 CMSIS-DSP 是 Arm Cortex-M 生态的库。openvela 中相应开关名为
`CONFIG_MLEARNING_CMSIS_NN`，并非 `TFLITE_ENABLE_CMSIS_NN=ON`；但把它用于 ESP32-S3
会引入 ARM 头文件、intrinsic 和汇编依赖，不能产生可用的 LX7 优化代码。

因此，以下操作均不应在本板执行：

- 为 ESP32-S3 打开 `CONFIG_MLEARNING_CMSIS_NN`；
- 用 CMSIS-DSP 的 RFFT 替换 KissFFT；
- 把 ARM NEON、Ethos-U 当作 ESP32-S3 的 TFLM backend。

### 3.2 Xtensa HiFi 不等于 ESP32-S3

openvela 的 `CONFIG_XTENSA_HIFI` 会替换为 `tensorflow/lite/micro/kernels/xtensa/` 中的
HiFi kernel，并按 `HIFI4` 构建 Cadence `xa_nnlib`。这些 kernel 只有在
`HIFI3/HIFI4/HIFI5` 宏成立时才走加速路径；ESP32-S3 的 LX7 不具备该 DSP ISA。

### 3.3 ESP-NN 的位置

ESP-NN 是 ESP32-S3 的正确专用 backend，现已通过 `CONFIG_TFLITEMICRO_ESP_NN` 工程化接入。
它由 ESP-NN 源码、TFLM Conv2D/DepthwiseConv2D wrapper、Kconfig 和受控 tensor 白名单共同构成，
不是打开 HiFi 或 CMSIS 开关即可获得的功能。选择它的理由是 LX7 平台匹配、当前模型卷积热点集中，且已在
reference 对照中获得五卷积逐字节一致的 11.1887× 整体 Invoke 加速；新增 Mean 性能 profile 已达到
19.6840×、端到端 hash 一致且 Mean 已通过逐字节验证。代价是 scratch Arena 增加到 44,356 B，并需要对每个
新模型重新验证节点准入。

## 4. 当前模型的优化画像

模型注册了 9 个算子：`Shape`、`StridedSlice`、`Pack`、`Reshape`、`Conv2D`、
`DepthwiseConv2D`、`Mean`、`FullyConnected`、`Softmax`。当前 ESP32-S3 构建没有选中
厂商 backend，int8 Conv、DepthwiseConv、FullyConnected 使用 TFLM 的通用 reference
integer 路径。历史逐算子测试已确认普通 `Conv2D`（约 79.5%）和
`DepthwiseConv2D`（约 16.1%）合计约占 95.6% 的 Invoke 时间；详见
[tflm_benchmark 算子剖析](../使用与调试/tflm_benchmark算子剖析.md)。由于该结果采用
10 ms 粒度计时，细小算子及毫秒级加速幅度仍须复测。

前端使用 49 帧、40 个 Mel bin、3 通道（log-mel、delta、delta-delta）的浮点计算：

1. 49 次 KissFFT；
2. 每帧构造 40 个三角 Mel 滤波器；
3. 计算一阶和二阶 delta；
4. 在 `event_classifier_predict()` 中把 5,880 个 float 量化并写入 int8 输入 tensor。

在全 reference 历史基线中，前端小于模型阶段；五卷积 ESP-NN 生效后，前端现为 60--70 ms，已经是
端到端链路最大的单项开销。

## 5. 优化路线与验收标准

### 阶段 A：建立可解释基线

1. 使用无 OLED 的 `P1-file-gate-nooled` profile 重测当前 M001；将结果写入
   [audio_event模型真机基准测试汇总](../项目基线/audio_event模型真机基准测试汇总.md)。
2. 使用独立的 `tflm_benchmark` 固件重测同一模型数组和生产 resolver，并先确认计时源
   不再以 10 ms 为粒度量化。它可输出各算子耗时和 Arena 分配。不可只依据
   `audio_event --profile` 的整体 `infer` 字段推断算子占比。
3. 保存模型 SHA-256、固件 SHA-256、完整编译命令和原始串口日志。

独立 ESP32-S3 配置、`tflm_benchmark --warmup/--repeat` 命令和主机端统计方式见
[tflm_benchmark 算子剖析](../使用与调试/tflm_benchmark算子剖析.md)。该 benchmark
固件用于算子占比；生产端到端时间仍使用未启用 TFLM debug 的 `P1-file-gate-nooled`。

验收：复现 Conv/DepthwiseConv 为主要瓶颈的历史结论，得到每个 `Conv2D`、
`DepthwiseConv2D`、`FullyConnected` 的可分辨时间占比，以及与 `audio_event` 模型阶段的
差异说明。

### 阶段 B：低风险代码优化

1. 保持 Arena 在内部 SRAM。仅 22,708 B 的实际占用不构成容量压力；移入 PSRAM 可能使
   热 tensor 访问变慢。
2. 增加 int8 特征输出路径，令 DSP 根据模型输入 scale/zero-point 直接写入 int8 buffer，
   再调用已有的 `event_classifier_predict_quantized()`。必须用同一 WAV 逐元素或逐次
   推理比对输出，确保量化舍入规则一致。
3. 预计算每个 Mel band 的有效 FFT bin 范围和三角权重，去掉运行时的 40 × 256 次
   Mel 区间判断；保留 TensorFlow 兼容的 DC-bin 处理、幅值定义与 float 精度。修改后必须
   与原特征结果做误差测试，不得为了速度改变训练/部署特征契约。
4. 用 `--no-oled` 进行性能测量；OLED 是端到端 UI 开销，不是 TFLM 推理时间。

验收：模型输出类别及概率误差在预先定义的容差内；P1 的 feature、infer、total P95 均
优于 M001 基线或明确记录无收益。

### 阶段 C：ESP-NN backend（已完成当前模型验证）

高分辨率 CCOUNT 已确认 Conv/DepthwiseConv 是当前模型的主要热点，因此在 ESP32-S3 上采用 ESP-NN，
而非与架构不匹配的 CMSIS-NN/CMSIS-DSP 或 HiFi。当前实现和正式结果见
[Reference 与 ESP-NN 性能对比操作手册](../项目基线/Reference与ESP-NN性能对比操作手册.md)。

1. 引入与本项目工具链、许可证兼容的 ESP-NN 源码；不复用 ESP-IDF 的整个运行时。
2. 新增 `CONFIG_TFLITEMICRO_ESP_NN`，限制为 ESP32 系列目标，且与 HiFi/CMSIS-NN backend
   互斥。
3. 为 TFLM `Conv2D`、`DepthwiseConv2D` 编写 wrapper；根据 profile 决定是否实现
   FullyConnected。wrapper 必须保留 TFLM 的 padding、stride、dilation、activation、
   per-channel multiplier/shift 与 tensor scratch 语义。
4. 构建系统须排除对应通用 kernel，避免重复符号；未知或不支持的参数组合必须明确回退
   reference kernel。
5. 分别做 kernel 单测、随机输入数值回归、完整 M001 WAV 回归、P1/P2 真机性能回归。

当前验收：五个卷积节点和 Mean 均已通过受控的逐字节 reference 对照；纯模型 mean `Invoke()` 为 17.591 ms，
应用文件播放中的既有 `feature + infer`
约 90--110 ms，满足 250 ms hop；需重新播放 WAV 才能给出 Mean 后的端到端数据。
后续替换模型或修改 wrapper 后，仍须重新执行逐节点 verify、组合 verify、100 次 CCOUNT 和真实 WAV 回归，
不能直接沿用本模型的 tensor ID 或性能结论。

ESP-NN 降低模型阶段后，float log-Mel 前端约 60--70 ms 已成为最大单项开销。前端的 CCOUNT 分解、
稀疏 Mel、流式缓存和数值回归策略见
[ESP32-S3 音频特征提取优化方案](特征提取优化方案.md)；它与本节的 ESP-NN kernel 优化相互独立，
不得改变模型特征契约后仍沿用原模型性能或精度结论。

## 6. 不建议的“优化”

| 做法 | 原因 |
| --- | --- |
| 在 ESP32-S3 启用 CMSIS-NN / CMSIS-DSP | 架构不匹配，不能提供 LX7 加速 |
| 启用 `CONFIG_XTENSA_HIFI` | HiFi DSP ISA 与 LX7 不兼容 |
| 用 `-mfix-esp32-psram-cache-issue` 代替 SIMD 优化 | 该 flag 是旧 ESP32 PSRAM erratum 修复，不是计算加速开关 |
| 将 Arena 放入 PSRAM | 当前 Arena 富余，且推理热数据延迟可能上升 |
| 只提高模型阈值或关闭日志来声称推理变快 | 这改变的是检测行为或测量噪声，不是 kernel 性能 |
| 未做数值回归就替换 Mel/量化实现 | 会破坏训练特征契约，速度提升没有意义 |

## 7. 决策记录模板

每次优化都在基准汇总文档中登记，并额外记录：

| 项目 | 记录内容 |
| --- | --- |
| 改动 ID | Git revision/补丁标识 |
| TFLM backend | reference / ESP-NN / 其他 |
| 编译参数 | 实际完整编译命令或可复现配置 |
| 算子级时间 | 各 Conv、DepthwiseConv、FC 的均值/P95 |
| 数值回归 | 输入集、允许误差、最大误差、是否通过 |
| P1/P2 真机结果 | feature、infer、total P95、实时系数 |
| 结论 | 保留、回退或继续优化的理由 |

## 8. 相关资料

- [模型真机基准测试汇总](../项目基线/audio_event模型真机基准测试汇总.md)：统一性能与事件指标口径。
- [性能与评估](../项目基线/性能与评估.md)：项目性能目标与离线评估口径。
- [TFLM Kconfig](../../../apps/mlearning/tflite-micro/Kconfig)：现有 TFLM 开关。
- [TFLM 构建配置](../../../apps/mlearning/tflite-micro/CMakeLists.txt)：`-O3` 与架构 backend 的源文件选择。
