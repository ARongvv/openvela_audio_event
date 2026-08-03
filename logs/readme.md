# Audio Event 日志目录

```text
logs/
├── readme.md                 # 本目录说明
├── 演示/
│   ├── 演示日志.md             # 演示过程和结论
│   └── 演示视频.mp4            # 演示视频
└── 测试日志/
    └── *.log                  # 原始串口测试记录
```

`测试日志/` 用于保存可复现的板端测试记录；`演示/` 存放面向演示的材料，不与原始性能日志混放。

## 原始日志约定

测试日志主要由 `picocom` 串口监视会话采集，保留 NSH 命令、固件启动信息、推理结果和 `--profile`
输出。因此 `.log` 中可能含有终端控制字符，这是原始串口记录的正常现象，不应手工清洗后覆盖原文件。

## 测试日志命名

```text
<模型架构和大小>_<NN 后端配置>_<输入音频标识>.log
```

| 字段 | 说明 |
| --- | --- |
| `模型架构和大小` | 模型网络架构及其规模标识，例如 `ds_cnn_small`。 |
| `NN 后端配置` | TFLM reference 或 ESP-NN 的节点组合，例如 `nn5`。 |
| `输入音频标识` | 被测 WAV 的简短标识，例如 `fileA`。详细文件名写入索引表。 |

## 测试日志索引

| 日志文件 | 模型 | NN 后端 | 输入音频 | 测试命令 | 结果摘要 |
| --- | --- | --- | --- | --- | --- |
| [ds_cnn_small_nn5_fileA.log](测试日志/ds_cnn_small_nn5_fileA.log) | `ds_cnn_small`，模型 11,984 B | `nn5`：ESP-NN 加速 3 个 Conv2D（23/25/27）和 2 个 DepthwiseConv2D（24/26） | `combined_A_pure.wav`，无底噪 | `audio_event --file /data/combined_A_pure.wav --profile --no-oled` | `status=0`；推理约 30–40 ms，端到端窗口约 100–110 ms。 |

后续记录按相同格式追加到表格末尾。性能比较应使用同一块开发板、相同固件时钟、相同输入 WAV 和相同
`--profile` 参数；如果条件不同，请在“结果摘要”中明确写出差异。

## 复测前提

待测 WAV 必须先写入 LittleFS，且路径与命令一致。测试完成后，日志末尾应包含：

```text
[power_gate] ... infer=... skip=...
[app] stopped, windows=... status=0
```
