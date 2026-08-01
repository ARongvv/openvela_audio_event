# audio_event 模型真机基准测试汇总

## 1. 文档目的

本文档是 `ccf_audioevent` 后续模型迭代的**真机基准汇总表和记录模板**。每次更换
模型、特征、阈值、检测器策略或能量门控策略后，均应在相同数据集和测试口径下新增
一条记录，而不覆盖旧结果。

本文件只汇总 ESP32-S3 端到端结果；离线训练集的 accuracy、F1 和混淆矩阵请参见
[性能与评估.md](性能与评估.md)。两类指标不可直接混算。

## 2. 使用规则

### 2.1 可横向比较的前提

只有同时满足下列条件的记录才可直接比较：

- 使用同一音频与同一 JSON 标注版本；
- 采样率、窗口长度、hop、特征契约和类别顺序相同；
- 告警阈值、连续命中、冷却策略、能量门控和 OLED 状态相同；
- 使用同一板卡、固件配置、供电方式；
- 每条记录都登记模型 SHA-256、固件 SHA-256、原始日志路径。

若任一条件不同，必须新建测试配置（profile），结果只能作为趋势参考。

### 2.2 指标口径

板端一条 `[infer] t=T` 代表以 `T` 为结束时间的 1 秒分析窗，即 `[T-1000, T]`。
目标事件告警与 JSON 标注区间重叠、且类别相同，即记为一次命中；每个真实事件最多
匹配一条同类告警。

| 指标 | 定义 |
| --- | --- |
| TP | 与同类标注事件匹配的告警数 |
| FP | 未匹配任何同类标注事件的告警数 |
| FN | 未被任何同类告警匹配的标注事件数 |
| Precision | `TP / (TP + FP)` |
| Recall | `TP / (TP + FN)` |
| F1 | `2 × Precision × Recall / (Precision + Recall)` |
| 推理跳过率 | `skip / (infer + skip)`，仅有门控统计时报告 |
| 实时系数 | `wall_time / audio_duration`；小于等于 1 才表示可实时完成 |

该口径评估的是“模型 + 能量门控 + 检测器”的端到端效果，不是逐帧分类准确率。

## 3. 固定数据集与测试配置

### 3.1 数据集注册表

| 数据集 ID | 文件与标注 | 格式/时长 | 事件构成 | 文件 SHA-256 |
| --- | --- | --- | --- | --- |
| `DS-A01` | `combined_A_pure.wav/.json` | 16 kHz、mono PCM16、163.2 s | 70 个事件，7 类各 10 个 | `ba05f19a340deab37ee9e55e32a628541083744e76571dbcca30f382d0e87916` |

`DS-A01` 的类别为：`knock`、`cough`、`dog_bark`、`glass_breaking`、`no`、
`stop`、`yes`。其中前两类为目标事件，其余五类均视为非目标负样本。

### 3.2 测试 profile 注册表

| Profile | 用途 | 输入 | 门控 | OLED | 状态 |
| --- | --- | --- | --- | --- | --- |
| `P0-file-gate-ui` | 历史基线，保留本次已完成实测 | LittleFS WAV | 开 | 开 | 已完成 |
| `P1-file-gate-nooled` | 后续模型的标准端侧事件/性能测试 | LittleFS WAV | 开 | 关 | 待执行 |
| `P2-file-nogate-nooled` | 分离模型能力与门控代价 | LittleFS WAV | 关 | 关 | 待实现 `--no-power-gate` |
| `P3-device-gate-nooled` | 真麦克风连续运行验证 | `/dev/audio/pcm_in1` | 开 | 关 | 待执行 |

`P0` 与 `P1/P2/P3` 不应直接横向比较总耗时，因为 `P0` 启用了 OLED。后续模型
排名应优先使用 `P1` 的结果；`P2` 用于量化门控造成的漏检与性能影响。

## 4. 模型注册表

每个参与比较的模型必须先在此表登记，再写入测试结果。

| 模型 ID | 模型来源与 SHA-256 | 大小/参数 | 输入与类别 | 备注 |
| --- | --- | ---: | --- | --- |
| `M001-small-int8` | `app/audio_event/model/model_int8.tflite`，`923dae7696718bb3824bbf075c77bad4de85ca606743000cee73fecb49d3d13b` | 11,984 B / 2,148 | int8 `[49,40,3]`，knock/cough/background/silence | 当前基线 |
| `Mxxx` | 待填写 | 待填写 | 待填写 | 新模型追加于此 |

`M001` 的 `model.cc` 数组与上述 `.tflite` SHA-256 一致。板端启动日志当前只打印
模型大小，不打印完整 SHA-256；后续应增加 `model_id`/短 SHA 的启动日志，避免同大小
模型被误当作同一版本。

## 5. 结果总表

### 5.1 端到端事件指标

| 模型 | 数据集 | Profile | 参数（knock/cough/连续/冷却） | TP/FP/FN | 总 Precision | 总 Recall | 总 F1 | cough R/P | knock R/P | 结论 |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- | --- |
| `M001-small-int8` | `DS-A01` | `P0-file-gate-ui` | 380‰ / 750‰ / 2 / 1500 ms | 13 / 39 / 7 | 25.0% | 65.0% | 36.1% | 80.0% / 17.8% | 50.0% / 71.4% | cough 严重误报，knock 漏检偏多 |
| `Mxxx` | `DS-A01` | `P1-file-gate-nooled` | 待填写 | 待填写 | 待填写 | 待填写 | 待填写 | 待填写 | 待填写 | 待填写 |

说明：`R` 为 recall，`P` 为 precision。当前 `M001` 的 52 条告警中，45 条为
cough、7 条为 knock；13 条正确匹配真实目标事件。

### 5.2 端侧资源与性能指标

| 模型 | 数据集/Profile | Arena 已用/配置 | 推理次数/跳过数 | 特征均值/P95 | 推理均值/P95 | 总处理均值/P95 | Wall/音频时长 | 备注 |
| --- | --- | --- | ---: | --- | --- | --- | ---: | --- |
| `M001-small-int8` | `DS-A01` / `P0-file-gate-ui` | 22,708 / 65,536 B | 539 / 110 | 66.4 / 70 ms | 349.6 / 350 ms | 544.3 / 860 ms | 2.06x | OLED/告警计入总耗时 |
| `Mxxx` | 待填写 | 待填写 | 待填写 | 待填写 | 待填写 | 待填写 | 待填写 | 待填写 |

`M001` 的门控跳过率为 `110 / (539 + 110) = 16.9%`。其推理均值已高于 250 ms
hop，无法在不跳窗的情况下实现 4 Hz 持续推理。

## 6. M001 基线的错误分析

### 6.1 目标事件

| 目标 | 标注事件 | 命中 | 漏检 | 主要原因 |
| --- | ---: | ---: | ---: | --- |
| cough | 10 | 8 | 2 | 一次最高概率仅 480‰；一次被前一条 knock 告警的全局冷却压制 |
| knock | 10 | 5 | 5 | 多数窗口被 cough/silence 判为 argmax，或无法连续两窗确认 |

最后一个真实 cough（156.641–157.641 s）在 157.750 s 已出现 965‰ cough 概率，
但 156.500 s 的 knock 告警使全局冷却持续到 158.000 s，因此未触发告警。这是
检测器策略问题，不是模型置信度不足。

### 6.2 非目标声音误报

| JSON 非目标类 | 事件数 | 重叠错误告警 | 告警分布 |
| --- | ---: | ---: | --- |
| stop | 10 | 10 | cough 9，knock 1 |
| dog_bark | 10 | 9 | cough 8，knock 1 |
| yes | 10 | 7 | cough 7 |
| glass_breaking | 10 | 5 | cough 5 |
| no | 10 | 5 | cough 5 |

`stop` 的 10 个事件全部造成错误告警，是下一轮训练的首要 hard negative。多条
误报的 cough 概率仍为 945‰–988‰，因此仅提高 cough 阈值不能根治问题。

### 6.3 当前检测器对结果的影响

当前检测器仅在 knock/cough 为全局 argmax 且超过阈值时建立候选，要求连续两个
窗口命中，并使用共享的 `g_cooldown_until_ms`。这会产生三个影响：

1. cough 误告警会在冷却期内压制真实 knock，反之亦然；
2. knock 概率较高但低于 cough 概率时，完全不会成为 knock 候选；
3. 严格连续两窗会受门控跳窗影响，对短暂 knock 不利。

因此，新模型的得分必须在相同 detector 配置下比较；若修改 detector，应登记为
新的测试 profile，而不是直接与旧行排名。

## 7. 标准复测流程

### 7.1 测试前登记

1. 记录模型 ID、`.tflite` SHA-256、`model.cc` 是否由同一次导出生成。
2. 记录 `nuttx.bin` SHA-256、板卡、供电方式、`.config` 中的相关配置。
3. 确认 LittleFS 中的 WAV SHA-256 与 `DS-A01` 注册表一致。
4. 确认数据集、profile 和阈值；若不同，注册新的 profile。

### 7.2 板端执行

为避免 NSH 行长度截断，先进入音频目录。`P1` 的标准命令为：

```sh
nsh> cd /data/audio
nsh> audio_event --file combined_A_pure.wav --no-oled --profile
```

PCM 输入质量应单独测试，不能与性能命令拼接：

```sh
nsh> audio_event --file combined_A_pure.wav --audio-stats
```

`P2` 需要实现 `--no-power-gate` 后再执行；不可用“未记录门控状态”的日志替代。

### 7.3 结果归档

每次测试应保存：

- 串口原始日志；
- 模型 `.tflite`、生成的 `model.cc`、模型 SHA-256；
- 固件 `nuttx.bin` SHA-256；
- 数据集 WAV/JSON SHA-256；
- 日志解析脚本版本与指标输出。

完成后，在第 4、5 节新增模型和结果行；若有异常模式，在下一节增加一条简短分析。

## 8. 新模型记录模板

将下列区块复制到文档末尾，填写完再把汇总数字同步到第 4、5 节。

```markdown
### Mxxx-<模型名> / <日期>

| 项目 | 值 |
| --- | --- |
| 模型 SHA-256 | `<sha256>` |
| 模型大小/参数量 | `<bytes>` / `<count>` |
| 固件 SHA-256 | `<sha256>` |
| 数据集/Profile | `DS-xxx` / `P?` |
| 特征、窗口、hop | `<contract>` |
| 阈值、连续、冷却 | `<knock>/<cough>/<hits>/<cooldown>` |
| 门控/OLED | `<on/off>` / `<on/off>` |

| 事件指标 | knock | cough | 合计 |
| --- | ---: | ---: | ---: |
| TP / FP / FN |  |  |  |
| Precision / Recall / F1 |  |  |  |

| 性能指标 | 值 |
| --- | ---: |
| Arena 已用/配置 |  |
| 推理次数/跳过数 |  |
| 特征均值/P95 |  |
| 推理均值/P95 |  |
| 总处理均值/P95 |  |
| Wall/音频时长 |  |

原始日志：`<path>`

结论与异常：`<简要说明>`
```

## 9. 优化优先级

1. 训练中加入 `stop`、dog bark、`yes/no`、glass breaking 作为 hard negative；
2. 将 knock/cough 改为独立候选状态和独立冷却时间；
3. 评估“独立目标概率 + margin”及 EMA/2-of-3 窗策略；
4. 实现 `--no-power-gate`，量化门控收益和漏检代价；
5. 用 `P1` 重测 M001，建立无 OLED 的可比较性能基线；
6. 若目标为持续麦克风检测，考虑至少 500 ms hop 或更快模型。

## 10. 原始材料

- 当前板端日志：`/home/arongw/openvela/audio_test3.log`
- `DS-A01` 音频：`/home/arongw/Documents/audio/micro_model/test_data/combined/combined_A_pure.wav`
- `DS-A01` 标注：`/home/arongw/Documents/audio/micro_model/test_data/combined/combined_A_pure.json`
- 模型元数据：`app/audio_event/model/metadata.json`
- 检测器实现：`app/audio_event/detector/event_detector.c`
