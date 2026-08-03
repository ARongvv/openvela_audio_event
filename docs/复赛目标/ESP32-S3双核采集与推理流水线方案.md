# ESP32-S3 双核采集与推理流水线方案

> 状态：方案阶段，尚未修改 `audio_event` 的 SMP 配置或线程亲和性代码。
> 目标：让 I2S 音频采集在模型推理期间持续进行，避免因计算负载导致采集链路断流；本方案不承诺缩短单次 TFLite Micro 推理时间。

## 1. 结论

ESP32-S3 有两个 LX7 CPU 核，可以将采集和推理固定到不同核心。但对本项目而言，
双核的主要收益是**采集实时性和时序隔离**，不是将一个模型的单线程 `Invoke()` 自动
加速一倍。

当前 BC-ResNet1 8-class 预检的实测稳态时间为约 1,190 ms/次；即使采集完全放到另一个
核心，推理核心的最高吞吐仍约为 0.84 次/秒。因此双核可以避免音频停止采集，却不能使
该模型满足当前 250 ms hop（4 次/秒）的更新目标。

## 2. 当前实现状态

`audio_event` 已经有两级逻辑流水线，无需额外创建“推理线程”：

```text
capture_thread_main()                         audio_event 主线程
----------------------                        ----------------------
audio_capture_read(512 samples)               等待目标样本数
  -> 写入 2 秒 PCM ring buffer                  -> 复制最新 1 秒窗口
  -> power_gate_process()                       -> feature_extract_compute()
  -> semaphore 唤醒主线程                       -> event_classifier_predict()
                                                -> detector / alert / UI
```

实现位置：

- `app/audio_event/main/audio_event_main.c`：采集线程、2 秒环形缓冲和主线程推理循环；
- `app/audio_event/audio/audio_capture.c`：NuttX Audio/I2S 异步缓冲转换为阻塞读取；
- `app/audio_event/dsp/feature_extract.c`：49 x 40 x 3 特征提取；
- `app/audio_event/model/event_classifier.cc`：单个 TFLM interpreter 和模型调用。

采集线程当前使用比主线程高 40 的 `SCHED_RR` 优先级。它能在单核系统中抢占推理，降低
I2S 缓冲耗尽风险；但 `board/esp32s3-devkit/configs/audio_event/defconfig` 当前没有
`CONFIG_SMP=y`，两个线程尚未获得物理并行执行能力。

## 3. 推荐双核分工

```text
CPU0
  I2S DMA / 音频驱动中断
  capture_thread_main()
  512-sample PCM block 写入、RMS/peak 门控统计

CPU1
  audio_event 主线程
  复制最新 1 秒窗口、log-mel/delta 特征提取
  TFLM Invoke、事件判决、告警、OLED/UI
```

原则如下：

- TFLM interpreter、输入 tensor、`g_features` 和 `g_probabilities` 只由 CPU1 的主线程访问；
  不对同一个 interpreter 并发调用 `Invoke()`。
- 采集线程只在持有 `g_capture_state.lock` 时写 ring buffer 和更新采样计数；主线程只在锁内
  复制一份完整窗口，随后在锁外执行特征提取和推理。
- 保持“处理最新窗口”的策略，不建立无界待推理队列。模型落后时应丢弃过期窗口，而不是让
  结果越来越滞后。
- I2S DMA/中断具体落在哪个核心由驱动决定；首版将采集线程固定到 CPU0 是合理默认值，但
  必须用真机无丢帧测试验证。如发现中断或 Wi-Fi 负载冲突，可交换两个线程的核心绑定。

## 4. 分阶段实施

### 阶段 A：仅启用 SMP

在 `board/esp32s3-devkit/configs/audio_event/defconfig` 增加：

```text
CONFIG_SMP=y
CONFIG_SMP_NCPUS=2
```

ESP32-S3 DevKit 的上游 `smp`、`audio` 配置均使用 `CONFIG_SMP_NCPUS=2`，可作为板级参考。
此阶段不改应用代码。NuttX 可在两个核之间调度已有采集线程和主线程，但调度位置不固定；
它用于先验证 SMP 启动、I2S 和现有应用功能正常。

### 阶段 B：增加可选的线程亲和性

仅在 `CONFIG_SMP` 下增加一个可关闭的应用选项，例如：

```text
CONFIG_EXAMPLES_AUDIO_EVENT_CPU_AFFINITY=y
```

实现要点：

1. 在 `capture_worker_start()` 中，创建线程前对 `pthread_attr_t` 调用
   `pthread_attr_setaffinity_np()`，以 `CPU_SET(0, &cpuset)` 固定采集线程到 CPU0。
2. 在主线程完成基础初始化、开始设备循环前，用
   `pthread_setaffinity_np(pthread_self(), ...)` 和 `CPU_SET(1, &cpuset)` 固定主线程到 CPU1。
3. 每次设置后打印 affinity 掩码和返回值；设置失败时记录错误并回退为未绑定调度，不能影响
   音频采集或推理主流程。
4. 非 SMP 构建中不编译这些调用，保持现有单核行为不变。

NuttX 已提供 `pthread_attr_setaffinity_np()` 与 `pthread_setaffinity_np()`；不需要引入
ESP-IDF 的 FreeRTOS `xTaskCreatePinnedToCore()` API。

### 阶段 C：时序与负载策略

现有默认 `CONFIG_EXAMPLES_AUDIO_EVENT_HOP_MS=250`。对于 1,190 ms 的 BC-ResNet，必须先将
运行策略调整为“不积压、只取最新窗口”；建议调试时把 hop 设为 `1000 ms`，减少无意义的
重复唤醒和日志。

但 1,000 ms hop 也不能让 1,190 ms 的推理达到严格 1 Hz：CPU1 仍会近似连续忙于推理。
要稳定覆盖 1 秒窗口，模型的 `feature + Invoke + 判决` 应留有余量，建议目标不高于
800--900 ms；若要支持 250 ms hop，则应以显著更小的模型或可靠 kernel 加速为前提。

## 5. 实时性解释

双核后的时间关系如下：

```text
0 ms       1000 ms                       2190 ms
|-----------|-------------------------------|
CPU0:  采集第一个 1 秒窗口  -> 持续采集下一段音频
CPU1:                         特征 + Invoke（约 1190 ms）
```

第一个结果的端到端延迟仍约为“1 秒音频窗口 + 处理时间”。连续运行时，CPU0 不会因 CPU1
推理而停止接收 PCM；但 CPU1 最多每约 1.19 秒输出一个最新窗口的结果，不能补偿缺失的
中间 250 ms 滑窗结果。

对比关系：

| 项目 | 单核（高优先级采集线程） | 双核 + affinity |
| --- | --- | --- |
| I2S 采集连续性 | 依赖抢占，推理负载高时存在抖动风险 | 与推理核心隔离，显著更稳健 |
| 单次 `Invoke()` | 约 1,190 ms（BC-ResNet 预检） | 仍约 1,190 ms，需实测确认共享资源影响 |
| 模型吞吐 | 受采集抢占影响 | 仍由 CPU1 模型时间决定，理论上不超过约 0.84 Hz |
| 250 ms hop | 不可持续 | 仍不可持续 |

## 6. 验证与验收

构建由开发者在本地执行，例如：

```bash
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/audio_event -j8
```

每阶段至少记录以下数据，并保存完整串口日志、固件 revision、模型 SHA-256 和 defconfig：

1. **SMP 启动**：确认启动日志/任务信息显示双核已启用；亲和性阶段还应打印主线程与采集线程
   的 CPU mask。
2. **采集稳定性**：连续设备采集至少 10 分钟；无 `audio_capture_read()` 错误、无 DMA buffer
   重新入队失败、无 ring buffer 异常。
3. **处理时序**：使用 `audio_event --device ... --profile`，记录 feature、infer、total 的
   平均值和 P95，以及音频时间戳与 wall 时间戳的差。
4. **识别回归**：对 knock、cough、background、silence 的冻结 PCM 和真机录音重复测试，比较
   单核、SMP 未绑定、SMP 绑定三种配置的类别、概率、事件触发和误报。
5. **资源回归**：检查线程栈余量、heap、CPU 负载和 PSRAM；双核运行时共享 cache/PSRAM
   竞争可能使 CPU1 的推理时间略有变化，不能假设它一定不变。

完成条件：

- [ ] `CONFIG_SMP=y`、`CONFIG_SMP_NCPUS=2` 的 audio_event 固件可稳定启动并采集。
- [ ] CPU0/CPU1 affinity 可记录、可关闭，并在设置失败时安全回退。
- [ ] 连续 10 分钟采集无错误，模型持续推理时没有可观察的音频断流。
- [ ] 与单核基线相比，识别结果和告警口径不发生非预期变化。
- [ ] 明确报告实际端到端延迟；不把“采集与推理并行”表述为“模型推理加速”。

## 7. 风险与边界

| 风险 | 影响 | 应对 |
| --- | --- | --- |
| 模型吞吐低于音频窗口产生速度 | 结果跳过窗口或延迟升高 | 只处理最新窗口；调整 hop；压缩模型或恢复稳定的 kernel 加速 |
| I2S 中断与固定线程核心不匹配 | 采集抖动或 buffer 错误 | 先做长时间真机测试；必要时交换 CPU0/CPU1 分工 |
| 把同一 TFLM interpreter 放到多线程 | 数据竞争、输出不确定或崩溃 | 保持 interpreter 和推理缓冲仅由一个线程使用 |
| 锁覆盖特征/推理过程 | 采集线程被阻塞 | 仅在 ring 写入、样本快照和门控状态访问时短暂持锁 |
| PSRAM/cache 共享竞争 | SMP 下推理时间波动 | 记录 P95；将热数据位置和配置纳入基准报告 |
| 只启用 SMP 不固定核心 | 结果不可复现 | 阶段 A 用于兼容性，性能结论以阶段 B 绑定后的测量为准 |

## 8. 与模型优化的关系

双核方案应与模型优化并行推进，但不能代替后者：

- 旧 4-class 模型为 11,984 B 的 INT8 DS-CNN，历史模型阶段约 350 ms，仍超过 250 ms hop；
- BC-ResNet1 8-class 模型为 129,360 B，当前 reference kernel 稳态约 1,190 ms；
- 对 BC-ResNet，优先考虑缩小网络、降低通道/Block 数或后续验证稳定的 ESP-NN；
- 双核首先保证“录音不中断”，模型压缩/算子加速才决定“多久产生一次分类结果”。

相关文档：

- [ESP32-S3 推理时间优化方案](../优化文档/ESP32-S3推理时间优化方案.md)
- [BC-ResNet1 8-class 模型预检](../项目基线/BCResNet1_8class模型预检.md)
- [低功耗策略方案](低功耗策略方案.md)
