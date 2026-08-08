# S3-large 8-class 端到端分类与告警测试

## 1. 目的与结论

本文记录 `s3_large_8class` INT8 模型在 ESP32-S3 DevKit 上运行完整
`audio_event` 链路的首次带标注测试：文件读取、能量门控、log-mel + delta
特征、TFLite Micro + ESP-NN 推理和告警器均在板端执行。

在本次 `combined_A_pure.wav` 测试中，按“推理窗与目标片段重叠至少 500 ms，
且至少一次 top-1 为目标类”的事件级口径，6 个模型内目标类别共 **60 段命中
51 段，事件级分类命中率为 85.0%**。端到端单窗口耗时为 **110--120 ms**，
低于 250 ms hop，不存在处理积压。

但该结论不能代替训练/验证集精度：测试开启了 energy gate 和 1.5 s cooldown，
会影响推理窗口覆盖率及告警触发次数；此外，未建模的 `dog_bark` 拒识效果较弱。

## 2. 测试对象与证据

| 项目 | 内容 |
| --- | --- |
| 板卡 | ESP32-S3 DevKit（N16R8） |
| 应用 profile | `audio_event_8class` |
| 模型 | `s3_large_8class` INT8，37,784 B，8 输出类 |
| 模型类别 | `knock`、`cough`、`glass_breaking`、`yes`、`no`、`stop`、`background`、`silence` |
| 输入文件 | `ccf_audioevent/test_data/combined_A_pure.wav`，约 163.2 s |
| 时间标注 | `ccf_audioevent/test_data/combined_A_pure.json` |
| 原始板端日志 | `ccf_audioevent/logs/测试日志/audio_event_8class测试.log` |
| 板端命令 | `audio_event --file /data/combined_A_pure.wav --profile --no-oled` |
| hop | 250 ms |
| 节能策略 | energy gate，启动校准 2 s，静音窗口可跳过推理 |
| 告警策略 | event 类连续命中；keyword 单窗阈值；cooldown=1500 ms |

启动日志确认内存布局和模型初始化正常：

```text
[app] audio buffers in PSRAM: ring=0x3c070010 window=0x3c07fa10 features=0x3c087b10
[model] arena ptr=0x3fc8e250 size=196608 align_256=0
[s3-large-8class] arena=196608 used=132692 model=37784 bytes
```

这表示约 121 KiB 音频工作区在 PSRAM，196,608 B tensor arena 位于内部 DRAM；
ESP-NN profile 的 arena 实际使用量为 132,692 B。

## 3. 标注集构成

JSON 标注共 70 个约 1 s 片段：

| 标注类别 | 数量 | 是否是模型输出类 | 评估用途 |
| --- | ---: | --- | --- |
| `cough` | 10 | 是 | 目标分类/告警 |
| `glass_breaking` | 10 | 是 | 目标分类/告警 |
| `knock` | 10 | 是 | 目标分类/告警 |
| `no` | 10 | 是 | 目标分类/告警 |
| `stop` | 10 | 是 | 目标分类/告警 |
| `yes` | 10 | 是 | 目标分类/告警 |
| `dog_bark` | 10 | 否 | 干扰拒识 |

`dog_bark` 不在 8-class 模型输出中，因此它不能参与 6 类精确分类 accuracy；
本报告以预测为 `background` 或 `silence` 作为“拒识正确”的代理指标。

## 4. 统计口径

应用中的每次推理使用最近 1 s 音频窗，日志中的 `t` 是该窗的结束时间。为避免只用
窗口边界而误伤分类结果，本文使用如下口径：

1. 对每个标注片段，寻找与其重叠至少 500 ms 的推理窗；
2. 若任意一个候选窗 top-1 等于标注类别，记为该事件分类命中；
3. 逐窗口指标只统计最大重叠不少于 500 ms 的窗口；
4. 告警指标依据 `[ALERT]`，同样以 500 ms 重叠匹配；
5. 未关闭 energy gate、未取消 cooldown，因此告警指标描述的是当前产品策略，
   不等同于纯模型召回率或正式漏报率。

## 5. 分类结果

### 5.1 事件级分类命中

| 类别 | 标注段数 | 命中段数 | 事件级命中率 |
| --- | ---: | ---: | ---: |
| cough | 10 | 8 | 80% |
| glass_breaking | 10 | 5 | 50% |
| knock | 10 | 10 | 100% |
| no | 10 | 9 | 90% |
| stop | 10 | 10 | 100% |
| yes | 10 | 9 | 90% |
| **合计** | **60** | **51** | **85.0%** |

有 2 个目标片段在 energy gate 条件下没有获得重叠至少 500 ms 的有效推理窗；
其余未命中主要属于类别混淆。`glass_breaking` 是最需要优先改进的类别。

### 5.2 逐窗口 top-1

在 230 个与模型内目标类别重叠至少 500 ms 的推理窗中，153 个 top-1 与标注相同：

```text
153 / 230 = 66.5%
```

逐窗口指标低于事件级命中是预期现象：片段边界处的 1 s 窗同时含有静音或相邻片段，
此时模型可出现短时混淆，但同一事件的其他窗仍可能被正确识别。

### 5.3 主要混淆

| 标注类别 | 主要 top-1 输出（有效窗数） | 观察 |
| --- | --- | --- |
| cough | cough 20；yes 11；no 5 | 咳嗽与关键词存在短窗混淆 |
| glass_breaking | yes 14；glass_breaking 12；cough 11 | 最弱类别，需补充训练与量化校准 |
| knock | knock 35；cough 4 | 区分度最好 |
| no | no 26；yes 4 | 少量 yes/no 混淆 |
| stop | stop 26；yes 9 | stop 与 yes 存在过渡窗混淆 |
| yes | yes 34；background 2 | 关键词识别稳定 |

## 6. 未建模干扰：dog_bark 拒识

`dog_bark` 的 39 个有效推理窗中，只有 4 个输出为 `background` 或 `silence`：

```text
4 / 39 = 10.3%
```

其余主要被误判为 `yes`（20 窗）、`cough`（8 窗）和 `no`（5 窗）。这表示当前
background 类对狗叫的覆盖不足，实际部署存在关键词误报风险。应将狗叫及其他常见环境声
加入 background/hard-negative 训练，或在后续模型中增加独立 dog_bark 类。

## 7. 当前告警策略结果

在现有 energy gate、阈值、连续命中和 1500 ms cooldown 下，60 个模型内目标片段中
29 段至少产生一次同类告警，覆盖率为 **48.3%**。这不能直接称为“模型准确率”，因为：

- energy gate 共跳过 110 个窗口；
- cooldown 会刻意压制时间相邻的告警；
- keyword 与 event 使用不同的触发规则。

该结果适合描述当前产品行为，不应用作正式漏报率。要计算告警级 Precision/Recall，
应使用专用评测 profile：关闭 energy gate、设置 cooldown 为 0，保留每个候选告警及其时间戳，
再按标注时间窗匹配。

## 8. 实时性结果

| 指标 | 板端观测值 | 结论 |
| --- | ---: | --- |
| 特征提取 | 60--70 ms | 主要前端开销 |
| S3-large ESP-NN 推理 | 50--60 ms | 含应用调用与计时粒度影响 |
| 单窗口总耗时 | 110--120 ms | 小于 250 ms hop |
| 文件音频时间 | 161.0 s | 日志最后一个处理窗口时间 |
| 实际 wall 时间 | 74.66 s | 离线处理约 2.16 倍实时 |
| 完成推理窗口 | 539 | energy gate 后的实际 Invoke 数 |
| 跳过窗口 | 110 | 静音/低能量节能跳过 |

结论：当前 S3-large + ESP-NN 在该板卡上具备实时余量；性能瓶颈已由卷积推理转向特征提取。

## 9. 后续评测与优化优先级

1. 新增不含 energy gate、cooldown=0 的评测 profile，分离模型分类能力与 detector 策略效果；
2. 对 `dog_bark`、人声、音乐、风噪等干扰建立 hard-negative 集，报告误报率；
3. 优先扩充并复查 `glass_breaking` 样本、频段分布和 INT8 校准集；
4. 对每类独立 WAV 统计 Precision、Recall、F1、平均触发延迟与混淆矩阵；
5. 在不同 SNR、距离和环境噪声条件下重复以上测试，形成比赛报告的鲁棒性图表。

## 10. 限制

- 本文只有一个组合文件和一次板端运行，不能代表泛化能力；
- `dog_bark` 不是模型输出类，拒识率是代理指标；
- JSON 的片段级标注与 1 s 滑动窗天然存在边界重叠，故逐窗口和事件级数值不能混用；
- 日志末尾的 `BROWNOUT_RST` 出现在应用以 `status=0` 正常结束之后，应作为独立供电问题处理，
  不计为本次模型推理错误。
