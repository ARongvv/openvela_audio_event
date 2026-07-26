# 方案 3：事件分类、语音门控与关键词检测级联实施方案

> 状态：实施设计，尚未编码。  
> 目标：在原有 `knock/cough/background/silence` 事件分类基础上，新增 `speech` 类；检测到人声后，将带 pre-roll 的 PCM 送入 WakeNet/KWS，仅在关键词命中后告警。

## 1. 方案范围和决策

目标链路如下：

```text
INMP441 -> PCM 环形缓冲区
  -> EventVADNet（knock/cough/speech/background/silence）
     -> knock/cough：沿用事件检测器和本地告警
     -> speech：speech gate 状态机 -> WakeNet/KWS -> 关键词检测器 -> 本地告警
     -> background/silence：不运行 WakeNet
```

首版关键词固定为 `yes`、`no`、`stop`。内部标签、训练目录、串口日志和 OLED 显示均直接使用这三个 ASCII 英文词；`yeah`、`nope`、中文“是/否/停止”等同义词不属于首版命中范围，除非后续明确加入训练和验收口径。

本方案的第一层称为 `EventVADNet`，它是**事件粗分类模型**，不是乐鑫 32 ms 帧级 VADNet 的等价替代。它的优势是复用现有事件模型链路；代价是当前事件窗口处理约 420–430 ms，因此关键词触发需依赖环形缓冲和严格的调度控制。

## 2. 前置验收

在修改业务代码前，以下条件必须通过：

1. 当前训练端和固件端的 PCM、特征、量化输入、TFLite 输出通过黄金向量对齐。
2. 当前 `knock/cough` 固定回归 PCM 在真机或等价文件模式下有可重复基线结果。
3. 明确关键词语义、允许的自然变体、非关键词人声边界和训练数据许可证。
4. 确认 WakeNet 的自定义关键词模型、授权、模型加载方式和 openvela/NuttX 可移植路径；未通过时使用同一接口的自训 INT8 TFLite Micro KWS 后端作为备选，不能阻塞事件模型改造。

## 3. 训练与模型资产

### 3.1 EventVADNet 重训练

第一层的类别顺序固定为：

```text
knock, cough, speech, background, silence
```

训练目录示例：

```text
datasets_event_vad/
├── knock/
├── cough/
├── speech/
└── background/
```

`silence` 继续由训练脚本生成。`speech/` 必须包含目标关键词、普通中文对话、非关键词短词、不同说话人、语速、距离和噪声条件；`background/` 包含音乐、风扇、键盘、交通、电视、非语音环境声。若评测使用扬声器播放关键词，扬声器人声也必须被 `speech` 接受，不能作为 background 拒绝。

初版可在现有训练脚本中使用：

```text
--target-classes knock,cough,speech
--background-class background
```

但输出模型前必须检查类别顺序、特征形状、量化参数、模型哈希和 `metadata.json` 是否同步。增加输出类别后应重新训练/完整评估；不能只替换最后一层而不重新验证旧事件。

### 3.2 WakeNet/KWS 资产

WakeNet/KWS 对外接口统一为：

```c
int keyword_classifier_init(void);
int keyword_classifier_process(const int16_t *pcm, size_t samples,
                               struct keyword_result_s *result);
void keyword_classifier_deinit(void);
```

接口不绑定具体后端：

| 后端 | 适用条件 | 风险 |
| --- | --- | --- |
| ESP-SR WakeNet | 已确认模型、依赖和 NuttX 移植路径 | AFE/模型加载/许可证与 ESP-IDF 耦合 |
| 自训 INT8 TFLM KWS | 可复用当前训练/模型封装方式 | 需自行实现帧平滑和关键词训练 |

无论后端，输入与输出必须记录采样率、帧长、特征、模型版本和关键词标签，且用黄金 PCM 建立桌面/板端回归。

## 4. 固件改造

### 4.1 类别和配置去硬编码

当前代码将 `AUDIO_EVENT_CLASS_COUNT` 固定为 4，目标类别和阈值固定为 `knock/cough`。实施时集中维护类别表：

```c
struct event_class_config_s
{
  const char *name;
  bool event_alert;
  bool speech_gate;
  uint16_t threshold_permille;
};
```

首版表包含五项，并由 `audio_event_config.h` 或模型元数据生成的头文件统一定义。`main`、`event_detector`、UI、日志和模型输出校验只读取这一份表；初始化时验证模型输出元素数和表长度一致。

Kconfig 预计新增：

| 配置 | 说明 |
| --- | --- |
| `EXAMPLES_AUDIO_EVENT_SPEECH_GATE` | 启用方案 3 |
| `EXAMPLES_AUDIO_EVENT_SPEECH_THRESHOLD` | `P(speech)` 门限，千分比 |
| `EXAMPLES_AUDIO_EVENT_SPEECH_HOLD_MS` | speech 状态保持时间 |
| `EXAMPLES_AUDIO_EVENT_KWS_PREROLL_MS` | 从环形缓冲取回的前导音频 |
| `EXAMPLES_AUDIO_EVENT_KWS_BACKEND` | WakeNet 或 TFLM KWS 后端选择 |
| `EXAMPLES_AUDIO_EVENT_KWS_QUEUE_DEPTH` | 首版固定为 1，防止积压 |
| `EXAMPLES_AUDIO_EVENT_KWS_COOLDOWN_MS` | 关键词重复告警冷却 |

最终参数必须通过校准集扫描确定，不能把文档中的示例值当成固化默认值。

### 4.2 模块新增与职责

```text
audio/                 已有：PCM 采集、格式转换
dsp/                   已有：EventVADNet 特征
model/event_classifier 已改：五类 EventVADNet
detector/              已改：事件和 speech gate
kws/                   新增：speech 状态、pre-roll、单槽任务队列
model/keyword_*        新增：WakeNet/TFLM KWS 适配器
alert/                 已改：支持 keyword 告警类型
ui_oled/               已改：显示 speech/KWS 状态和关键词
main/                  已改：模块初始化、调度和异常回收
```

`audio_capture` 线程继续只负责最高优先级的采集和写环形缓冲区；不得在该线程调用 EventVADNet、WakeNet、网络或 OLED。

### 4.3 speech gate 状态机

`EventVADNet` 每次完成窗口推理后使用 `P(speech)` 更新状态，而不是仅使用 argmax：

```text
IDLE
  P(speech) >= threshold -> SPEECH_ACTIVE

SPEECH_ACTIVE
  记录最近 pre_roll_ms PCM，向 KWS 单槽队列提交“最新窗口”
  P(speech) 低于 threshold 后保持 speech_hold_ms
  保持结束 -> IDLE
```

关键词任务正在执行或队列已满时，新语音窗口覆盖旧的待处理窗口。原因是过期关键词结果没有价值，而采集连续性比逐条排队更重要。记录 `kws_queue_overwrite_count` 用于性能测试。

`knock/cough` 仍使用现有事件检测器独立判决；`speech` 不直接触发告警；`background/silence` 只清理事件候选，不调用 KWS。

### 4.4 pre-roll 和窗口拼接

当前应用已有 2 秒 PCM 环形缓冲区，可直接复用。EventVADNet 判定 `speech` 时：

1. 读取触发前 `KWS_PREROLL_MS` 的 PCM；
2. 取当前窗口和后续 `speech_hold_ms` 内的新 PCM；
3. 按关键词后端要求组成连续窗口或按帧送入；
4. 若数据不足，等待下一块 PCM，但设置最大等待时间；
5. 对窗口打上单调时间戳，便于计算端到端延迟。

不得从触发时刻才开始录制；否则短词首字会被截断。若 WakeNet 后端采用 32 ms 流式帧，应把缓存以连续帧方式补送，并避免重复处理已确认的旧帧。

### 4.5 调度、内存和失败回退

首版采用串行模型执行，优先级为：

```text
audio capture > EventVADNet/event detector > KWS worker > OLED/log/report
```

不要在没有实际调度/arena 测量前并行调用两套推理器。两模型若使用独立 arena，必须统计 RAM/PSRAM 峰值；若尝试复用 arena，必须验证 interpreter 生命周期、模型切换开销和线程互斥。

失败行为：

| 失败 | 回退 |
| --- | --- |
| EventVADNet 初始化失败 | 应用报错退出，不能伪造类别结果 |
| KWS 初始化失败 | 明确标记 `kws=disabled`；若配置允许，knock/cough 事件检测继续运行 |
| KWS 运行失败 | 记录计数；丢弃当前关键词窗口；不阻塞下一次事件推理 |
| 音频设备失败 | 复用现有释放/重初始化路径；恢复后重置 speech/KWS 状态 |
| OLED/上报失败 | 本地模型和采集继续运行 |

## 5. 实施阶段

| 阶段 | 改动 | 验收 |
| --- | --- | --- |
| I0 | 冻结关键词/语音语义、模型后端和资源预算 | 数据说明、模型/许可可行性结论 |
| I1 | 训练五类 EventVADNet，导出资产和黄金向量 | 输出维度/类别/量化正确；旧事件离线回归不退化 |
| I2 | 类别表、EventVADNet 和五类日志接入固件 | T1 真机运行；knock/cough 与 speech 输出可见 |
| I3 | `kws/` 状态机、pre-roll、单槽队列 | speech 触发后首字未截断；队列不积压 |
| I4 | WakeNet/KWS 适配器和关键词检测器 | 每个关键词有黄金 PCM、板端日志和本地告警 |
| I5 | UI、异常回退、性能功耗测试 | 按[级联测试方案](VADNet_WakeNet级联测试方案.md)完成 T0–T3 对比 |

## 6. 功能验收

- [ ] 五类 EventVADNet 在固定回归集上能识别 `knock/cough/speech/background/silence`，并保持原事件能力。
- [ ] `P(speech)` 门控而非 argmax 触发 KWS；普通语音可触发 KWS 计算但不应直接告警。
- [ ] 关键词首字紧邻 speech 触发时，pre-roll 后的 KWS 仍能获得完整音频。
- [ ] 至少两个关键词在目标板上离线触发；无网络依赖。
- [ ] KWS 队列满、KWS 失败、OLED 失败和音频恢复不会阻塞采集或事件告警。
- [ ] T3 与 T0 的 knock/cough 事件回归、RAM、延迟、功耗和稳定性均有实测记录。

## 7. 主要风险

| 风险 | 影响 | 处理 |
| --- | --- | --- |
| `speech` 数据不覆盖关键词/远距离语音 | 前级门控漏检，WakeNet 不运行 | speech 类加入关键词、普通语音、未见说话人和板端实录 |
| 当前事件推理过慢 | KWS 结果滞后或任务积压 | 单槽最新窗口、pre-roll、分项耗时测试；必要时缩小 EventVADNet |
| WakeNet 依赖不能直接移植 NuttX | KWS 后端无法构建 | I0 先做兼容 POC；保持自训 INT8 TFLM 后端备选 |
| 两份模型 arena 超预算 | 初始化失败或运行不稳定 | I0/I4 逐步测量；先关闭 OLED/网络做最小资源基线 |
| `speech` 误激活太多 | WakeNet 功耗收益不足 | 报告实际调用率；用校准集调门限，不牺牲端到端召回 |
| 将 TV/音乐误定义为 background | 评测回放关键词被前级过滤 | 明确扬声器人声必须属于 speech，加入训练/测试 |
