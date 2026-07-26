# EventVADNet → WakeNet 级联性能、功耗与稳定性测试方案

> 状态：测试方案，尚未实施。  
> 目标：在同一块 ESP32-S3-N16R8 + INMP441 板上，量化“事件模型增加 `speech` 类，并以 `speech` 门控 WakeNet/KWS”的资源、持续 CPU 和实时性成本；用 T0–T3 对比证明门控是否在不损失关键功能的前提下成立。

## 1. 测试边界与原则

本方案参考乐鑫 [ESP-SR benchmark](https://github.com/espressif/esp-sr/blob/master/docs/zh_CN/benchmark/README.rst) 的指标命名和分项记录方式，但不复制其数值。乐鑫的测试板、麦克风阵列、ESP-IDF/AFE 管线与本项目不同；本方案只报告本项目在 openvela/NuttX、单 INMP441 上的实测结果。

本次性能指标固定分为三组，所有结果必须同时给出模型版本、固件 Git revision、Kconfig、板卡、CPU 频率、供电方式和测试日期。

| 指标组 | 必报指标 | 要回答的问题 |
| --- | --- | --- |
| 内存占用 | Internal RAM、PSRAM | 级联后是否仍有内部 RAM 余量；是否把实时关键数据放到了 PSRAM。 |
| 持续运行 CPU 占用 | Feed CPU、Fetch CPU | 连续音频输入时，喂音频与取模型结果两条路径各占用多少一个 CPU 核的时间。 |
| 单模型实时性能 | Average Running Time per Frame、Frame Length | 单个模型处理一帧需要多久，是否小于该帧代表的音频时长。 |

Flash、功耗、端到端识别、异常恢复和长时稳定性仍是辅助验证项，但不能替代上表三组核心指标。

## 2. 被测构建档位

所有档位必须使用同一块板、同一 INMP441、相同 I2S 参数、编译优化级别、日志等级和测试 PCM。T2/T3 的 WakeNet/KWS 模型、关键词（`yes`、`no`、`stop`）、阈值、冷却时间必须相同；唯一变量是 WakeNet 是否由 `speech` 状态门控。

| 档位 | 内容 | 用途 |
| --- | --- | --- |
| T0 | 当前事件模型：`knock/cough/background/silence` | 既有功能与资源基线。 |
| T1 | EventVADNet：`knock/cough/speech/background/silence`；WakeNet 关闭 | 分离出增加 `speech` 类的增量。 |
| T2 | EventVADNet + WakeNet 始终运行 | 关键词功能与最大持续负载对照。 |
| T3 | EventVADNet + `speech` gate + WakeNet | 目标级联实现。 |

单模型测试还必须单独执行：EventVADNet（T1 的事件模型）和 WakeNet/KWS（与 T2/T3 相同版本）。不要把两个模型叠加后的耗时误填为“单模型实时性能”。

## 3. 统一测试环境

### 3.1 固定配置

每次测试记录以下内容；任一项变化即创建新的结果批次，不与旧批次求平均。

```text
板卡/芯片版本、Flash/PSRAM 容量、固件 SHA、模型 SHA-256
INMP441 型号、接线、声道 slot、right-shift、安装位置和朝向
I2S：采样率、位宽、声道、DMA 描述符和音频缓冲配置
CPU 频率、单核/双核与任务 affinity、调度优先级、日志/OLED/Wi-Fi 状态
供电电压、功耗仪器、测量点、采样率
测试 PCM 的 SHA-256、时长、采样率、声道及循环方式
```

性能测量优先使用保存在板端或主机回放的固定 PCM，避免讲话者、距离和环境噪声改变 CPU 结果。功能、距离和噪声测试另按第 8 节执行。

### 3.2 持续运行窗口

每个“持续 CPU”单元先预热 60 秒，再连续运行 10 分钟，至少重复 3 次。报告每次原始值、均值、最小/最大值；不要只挑选最优一次。

测试 PCM 至少包含：静音/背景、普通连续人声、`yes`/`no`/`stop`，以及 knock/cough。T2/T3 还应增加连续人声噪声 PCM，验证门控在最坏情况下会接近始终运行。

## 4. 三组核心指标与统一口径

### 4.1 内存占用：Internal RAM、PSRAM

| 指标 | 单位 | 定义与取值时机 |
| --- | ---: | --- |
| Internal RAM | B / KiB | 内部 SRAM 中 `.data/.bss`、静态 Tensor Arena、任务栈、内部 heap 的已用/峰值；分别记录模型初始化后、10 分钟稳定运行峰值、结束值。 |
| PSRAM | B / KiB | 外部 PSRAM 中静态段及 heap 的已用/峰值；同样记录三个时机，并注明是否存在 PSRAM 分配。 |

记录方法必须同时包含两类证据：

1. 正确 ESP32-S3 构建的 `nuttx.map`：按段确认静态对象（尤其是 Event/Wake 的 Arena、PCM 环形缓冲、特征缓存）实际地址属于内部 SRAM 还是 PSRAM；
2. 运行时 heap/memdump：记录初始化前后和运行峰值的空闲量、最大连续块、分配失败次数。

不得只因 `CONFIG_ESP32S3_SPIRAM=y` 就把对象记为 PSRAM。没有外部段属性或 map 地址证据的静态数组，不能声称已迁移到 PSRAM。

推荐结果格式：

| 档位 | 时机 | Internal RAM 已用/峰值 | Internal RAM 最大连续块 | PSRAM 已用/峰值 | PSRAM 最大连续块 | Arena 位置与大小 | 证据 |
| --- | --- | ---: | ---: | ---: | ---: | --- | --- |
| T0 | 初始化后 / 稳定峰值 / 结束 |  |  |  |  | Event:  | map + 日志 |
| T1 | 初始化后 / 稳定峰值 / 结束 |  |  |  |  | Event:  | map + 日志 |
| T2 | 初始化后 / 稳定峰值 / 结束 |  |  |  |  | Event + Wake:  | map + 日志 |
| T3 | 初始化后 / 稳定峰值 / 结束 |  |  |  |  | Event + Wake:  | map + 日志 |

### 4.2 持续运行 CPU：Feed CPU、Fetch CPU

这里的 CPU 是**持续输入 10 分钟时，每条实时路径消耗的一个 CPU 核时间百分比**，不是某次 `Invoke()` 的瞬时耗时，也不是任务处于阻塞等待状态的墙钟时间。

| 指标 | 边界 | 计算方式 |
| --- | --- | --- |
| Feed CPU | PCM 获取、格式转换、写环形缓冲、按模型帧长切分并送入模型/队列；不含等待 I2S、队列和信号量的阻塞时间 | `Feed CPU = Feed 任务累计忙碌时间 / 测量窗口墙钟时间 × 100%` |
| Fetch CPU | 从完整帧可用开始，执行特征/模型运行、结果读取、平滑、阈值状态机及 KWS 结果处理；不含阻塞等待下一帧 | `Fetch CPU = Fetch 任务累计忙碌时间 / 测量窗口墙钟时间 × 100%` |

若为单核构建，两个百分比均以该核为 100%，但不能简单相加当作系统总 CPU（还存在 NuttX、I2S 驱动、日志和空闲任务）。若为双核构建，必须另报每个任务的 core affinity；跨核任务按其实际运行核分别累计，禁止把两个核相加后报成“100%”。

采样实现要求：在 Feed/Fetch 路径的实际工作段前后读取单调高精度时钟，累计 `busy_us` 和处理帧数；日志打印、OLED 刷新和串口阻塞应关闭或单独计入“非模型开销”。同时记录 PCM 短读、队列覆盖、过期帧丢弃和模型调用次数，防止通过少处理音频而得到虚低 CPU。

推荐结果格式：

| 档位 | PCM 场景 | Feed CPU 平均/最大 | Fetch CPU 平均/最大 | Event 调用/min | Wake 调用/min | PCM 短读/丢帧 | 队列覆盖/过期帧 | 核与频率 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| T0 | 背景 / 连续人声 |  |  |  | - |  |  |  |
| T1 | 背景 / 连续人声 |  |  |  | 0 |  |  |  |
| T2 | 背景 / 连续人声 |  |  |  |  |  |  |  |
| T3 | 背景 / 连续人声 |  |  |  |  |  |  |  |

### 4.3 单模型实时性能：Average Running Time per Frame、Frame Length

该组只测一个模型，且不包含 I2S 等待、队列等待、其他模型运行和 OLED/串口输出。它回答“模型拿到一帧完整音频后，能否在下一帧到来前处理完成”。

| 指标 | 单位 | 定义 |
| --- | ---: | --- |
| Frame Length | ms | 每次送入该模型的一帧所代表的原始音频时长；按 `frame_samples / sample_rate × 1000` 计算，并同时记录样本数、采样率和帧移/hop。 |
| Average Running Time per Frame | ms 或 us | 从该模型一帧数据就绪，到该模型完成前端处理、量化、`Invoke`/WakeNet detect、结果读取和本模型平滑判定的累计运行时间除以处理帧数；不含等待。 |
| RTF（派生指标） | 无量纲 | `Average Running Time per Frame / Frame Length`；小于 1 才具备不积压的实时基础。 |

模型边界必须写清：

| 单模型 | Running Time 包含 | 不包含 |
| --- | --- | --- |
| EventVADNet | log-mel/delta 特征、INT8 量化、TFLM `Invoke`、输出反量化与该模型的类别平滑 | PCM 采集、WakeNet、队列等待、告警 UI/串口。 |
| WakeNet/KWS | 单帧/单窗口送入 WakeNet 后的内部前端、detect、关键词平滑和结果读取 | EventVADNet、speech gate 判定、音频采集、队列等待、告警 UI/串口。 |

当前 EventVADNet 的输入窗口预计为 1,000 ms；WakeNet 常见输入为 16 kHz 的短帧，但最终表中必须填写**实际 API 和模型配置的 Frame Length**，不能把 30 ms 当作未验证事实。每个单模型至少处理 1,000 帧或连续运行 10 分钟（取较长者），报告均值、P95、最大值和 RTF。

推荐结果格式：

| 单模型 | 模型 SHA | 输入格式 | Frame Length | 帧移/hop | 样本数 | Average Running Time per Frame | P95 / 最大 | RTF | 处理帧数 |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| EventVADNet |  | 16 kHz / mono / s16 |  |  |  |  |  |  |  |
| WakeNet/KWS |  | 16 kHz / mono / s16 |  |  |  |  |  |  |  |

## 5. 端到端与功耗辅助指标

三组核心指标通过后，才比较级联带来的功能与功耗价值。

| 项目 | 记录内容 |
| --- | --- |
| Flash | 总镜像大小，Event/Wake 模型嵌入数组大小及相对 T0 的增量。 |
| 功耗 | 相同测量点下的平均电流、峰值电流、平均功率、累计能量；每档位/场景至少 10 分钟、3 次重复。 |
| 端到端延迟 | `yes/no/stop` 起始至本地 `ALERT` 的均值、P95、最大值；另记录 knock/cough。 |
| 功能 | 关键词 Recall、关键词 FA/h、knock/cough Recall、事件 FP/min、speech gate 漏检率。 |

功耗应至少比较背景和连续人声两种 PCM：T3 在背景下 Wake 调用减少，而连续人声时可能接近 T2，这是正常结果，必须同时展示。仅在同一场景下计算节能率：

```text
saving = (P_T2 - P_T3) / P_T2 × 100%
```

## 6. 真实声学场景与稳定性

性能 PCM 测试完成后，使用真实扬声器/说话人测试下列条件；记录距离、声压或实际 SNR、麦克风位置和朝向。

| ID | 距离 | 背景 | 目的 |
| --- | ---: | --- | --- |
| P1 | 1 m、3 m | 安静 | 基础识别、延迟与回归。 |
| P2 | 1 m、3 m | 固定噪声，目标 SNR 约 4 dB | 稳态噪声下的召回、FA 与 CPU。 |
| P3 | 1 m、3 m | 语音噪声，目标 SNR 约 4 dB | speech gate 的最坏持续负载。 |
| P4 | 0.5 m | 上述三类背景 | 近场补充。 |
| P5 | 无目标事件 | 各背景至少 1 小时，总计至少 12 小时 | 崩溃、内存增长、误触发和恢复。 |

长时运行必须记录：重启/崩溃次数、Internal RAM/PSRAM 结束值与初始值差、分配失败、PCM 读错误、队列高水位、模型初始化失败、关键词/事件误触发率。故障注入至少包括音频短读、WakeNet 初始化失败和 KWS 队列满；任何故障不得让采集线程永久停止。

## 7. 判定规则

- 同档位的三次持续 CPU 测试中不得有 PCM 丢帧、队列持续增长或处理过期语音；若发生，CPU 数值无效，先修复调度。
- 每个单模型必须报告 Frame Length、Average Running Time per Frame、P95、最大值和 RTF；`RTF >= 1` 表示该模型在该帧配置下没有实时余量。
- T3 的 Internal RAM、PSRAM 必须以 map 和运行时日志证明，不能只给模型 Arena 配置值。
- 只有在 T3 的关键词 Recall、FA/h、knock/cough 回归均可接受时，才比较它相对 T2 的 Wake CPU、Wake 调用率和功耗收益。
- 指标阈值以 T0/T2 实测基线和复赛要求共同确定；本方案不在没有基线数据前虚设百分比门槛。

## 8. 完成清单

- [ ] T0–T3 使用相同 PCM、相同板和相同日志策略完成 Internal RAM、PSRAM、Feed CPU、Fetch CPU 对比。
- [ ] EventVADNet 与 WakeNet/KWS 分别完成单模型 Frame Length、Average Running Time per Frame、P95、最大值、RTF 测试。
- [ ] 每条持续 CPU 数据预热 60 秒、运行 10 分钟、重复至少 3 次，并保存原始日志。
- [ ] 输出正确 ESP32-S3 `nuttx.map`、运行时 heap/memdump、模型 SHA、Kconfig、测试 PCM SHA 作为证据。
- [ ] 完成 P1–P5 的关键词/事件回归、功耗和至少 12 小时稳定性测试。
