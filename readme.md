# 基于 openvela 的端侧音频事件检测系统

`ccf_audioevent` 是一个运行在 **ESP32-S3-N16R8 DevKit** 上的离线音频事件检测项目。
它以 INMP441 数字麦克风或 LittleFS 中的 WAV 文件为输入，在设备端完成 PCM 采集、
Log-Mel + Delta 特征提取、TFLite Micro INT8 推理、事件判决和本地告警；在可信局域网中还可选
通过 HTTP 上报告警，并通过 UDP 显示实时 PCM 波形。

当前建议使用的主线是 **S3-large 8-class INT8 模型 + ESP-NN**。模型与推理完全在端侧执行；
网络能力是可选的可视化/上报扩展，网络不可用时不会成为本地识别的前置条件。

> 本仓库需要位于 openvela 工作区根目录下，例如
> `/home/arongw/openvela/ccf_audioevent`。下文将 openvela 根目录记为 `$OPENVELA_ROOT`。

## 项目能力一览

| 能力 | 当前实现 | 说明 |
| --- | --- | --- |
| 本地音频识别 | 16 kHz、单声道 PCM16 输入 | 支持 INMP441 I2S 采集和 LittleFS/WAV 文件回放。 |
| 特征提取 | Log-Mel + 一阶/二阶 Delta，`[49, 40, 3]` | 1 s 分析窗口、250 ms hop；内部使用 30 ms 帧、20 ms 帧移和 512 点 FFT。 |
| 主模型 | S3-large 8-class INT8，37,784 B | `knock`、`cough`、`glass_breaking`、`yes`、`no`、`stop`、`background`、`silence`。 |
| 推理加速 | ESP-NN：4 Conv2D、3 DepthwiseConv2D、Mean | 通过 TFLM 外层适配、节点选择、对齐缓冲、reference 回退与逐字节验证接入。 |
| 本地输出 | 串口、OLED、事件阈值/连续命中/冷却判决 | 不依赖 Wi-Fi 或上位机。 |
| 远程告警（可选） | HTTP POST JSON、异步有界队列 | 用于可信局域网；网络失败不会阻塞采集、推理或本地告警。 |
| 波形可视化（可选） | UDP PCM → Python receiver → 浏览器 Canvas | 默认关闭；仅用于可信局域网监看，允许少量丢包。 |
| 低功耗策略（可选） | PCM 能量门控 | 已实现运行时 `--power-gate` 开关；默认连续推理，尚未以电流仪完成物理功耗结论。 |

## 已测性能快照

以下是同一块 ESP32-S3、240 MHz、固定 `pattern` 输入、CCOUNT 计时得到的**模型内 Invoke**
数据。它不包含音频采集、特征提取、日志、OLED 或网络开销。

| 模型 / 后端 | 平均 Invoke | P95 | Tensor arena 实际使用 | 数值一致性 |
| --- | ---: | ---: | ---: | --- |
| S3-large 8-class，TFLM reference | 2,954.965 ms | 2,954.990 ms | 85,876 B | 基线 hash：`0xc18cf11e` |
| S3-large 8-class，完整 ESP-NN | 47.282 ms | 47.308 ms | 132,692 B | 与 reference hash 相同 |

完整 ESP-NN 相对 reference 的平均加速约 **62.5×**。端到端文件回放测试中，主线 8-class
配置的特征提取约 60–70 ms、推理约 50–60 ms、单窗口总处理约 110–120 ms，低于 250 ms hop。
测试条件、原始口径和限制见
[Reference 与 ESP-NN 性能对比](docs/复赛目标/renference与esp-nn对比.md)及
[S3-large 8-class 端到端分类与告警测试](docs/模型测试/S3-large_8class_端到端分类与告警测试.md)。

## 系统架构

```text
INMP441 I2S / LittleFS WAV
            │
            ▼
  PCM16 ring buffer（16 kHz、mono）
            │
            ├── 可选：有界 PSRAM 队列 → UDP PCM → 上位机波形
            ▼
Log-Mel + Delta / Delta-Delta 特征：[49, 40, 3]
            ▼
TFLite Micro INT8 模型
            │
            ├── Conv2D / DepthwiseConv2D / Mean → ESP-NN
            └── 其他算子 → TFLM reference
            ▼
阈值、连续命中、冷却时间判决
            │
            ├── 串口 / OLED 本地告警
            └── 可选：有界队列 → HTTP POST → 上位机 Dashboard
```

## 目录说明

```text
ccf_audioevent/
├── app/audio_event/                 # 主应用：采集、DSP、模型、判决、告警、网络扩展
│   ├── audio/                        # WAV/I2S 输入与 INMP441 32-bit → PCM16 适配
│   ├── dsp/                          # Log-Mel + Delta 特征提取
│   ├── model/                        # 4-class、S3-large 8-class 模型与 TFLM classifier
│   ├── reporter/                     # 异步 HTTP 告警上报
│   ├── streamer/                     # 可选 UDP PCM 发送
│   ├── power/                        # PCM 能量门控
│   └── tflm_benchmark/               # 独立模型/算子基准程序
├── board/esp32s3-devkit/configs/     # ESP32-S3 构建 profile
├── third_party/esp-nn/               # 固定版本 ESP-NN 上游源码（不在此修改）
├── third_party/tflm-espnn-adapter/   # 本项目维护的 TFLM ESP-NN 外层适配源码
├── patches/tflite-micro/             # 对 apps/mlearning/tflite-micro 的最小补丁
├── scripts/                          # 建链、LittleFS 镜像、基准汇总和上位机接收端脚本
├── test_data/                        # 文件回放测试数据与标注
├── logs/                             # 串口原始日志、演示材料和命名约定
└── docs/                             # 设计、性能、测试、部署和答辩材料
```

更详细的文件职责见 [docs/目录架构说明.md](docs/目录架构说明.md)。

## 前置条件与工作区接入

1. 准备能正常构建 ESP32-S3 的 openvela 工作区，并确认在根目录可执行 `./build.sh`。
2. 将本仓库放入 openvela 根目录，目录名为 `ccf_audioevent`。
3. 为主应用和定制板建立软链接。首次建立前请先确认目标路径没有用户文件；已有正确链接时无需重复创建。

```bash
cd /path/to/openvela
export OPENVELA_ROOT="$PWD"

ln -s "$OPENVELA_ROOT/ccf_audioevent/app/audio_event" \
  "$OPENVELA_ROOT/apps/examples/audio_event"
ln -s "$OPENVELA_ROOT/ccf_audioevent/board/esp32s3-devkit" \
  "$OPENVELA_ROOT/vendor/espressif/boards/esp32s3/esp32s3-devkit"
```

4. 建立 ESP-NN 源码链接。该脚本会拒绝覆盖意外存在的路径，可重复执行。

```bash
cd "$OPENVELA_ROOT"
./ccf_audioevent/scripts/link_esp_nn.sh
./ccf_audioevent/scripts/link_tflm_espnn_adapter.sh --check
```

`link_tflm_espnn_adapter.sh --check` 默认只校验，**不会修改** `apps/`。当前工作区若已存在
等价的适配源码，可继续使用；如需将该目录改为软链接，请先阅读
[外层适配说明](third_party/tflm-espnn-adapter/README.md)，并由维护者确认后显式执行 `--link`。

5. 对于 ESP-NN 构建，应用 TFLM 最小补丁，或确认当前 `apps/mlearning/tflite-micro` 已含等价修改：

```bash
cd "$OPENVELA_ROOT"
git -C apps/mlearning/tflite-micro apply --check \
  ccf_audioevent/patches/tflite-micro/0001-add-esp-nn-backend.patch
```

若 `apply --check` 报“补丁已应用”或上下文不匹配，不要强行重复应用；先参考
[补丁说明](patches/tflite-micro/README.md)确认当前工作区状态。

## 推荐构建与烧录

### 本地 8-class ESP-NN 主线

```bash
cd /path/to/openvela
./ccf_audioevent/scripts/link_esp_nn.sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/audio_event_8class -j8

cd nuttx
make flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BAUD=921600
cd ..
picocom -b 115200 /dev/ttyACM0
```

烧录端口因主机而异；请以实际设备节点替换 `/dev/ttyACM0`。首次构建、切换 profile 或修改
Kconfig 后，构建系统会重新配置，耗时会比增量构建更长。

在 NSH 中先完成模型冒烟测试：

```text
nsh> audio_event --model-smoke
```

预期能看到 `s3-large-8class`、arena 使用量和 8 个类别概率。出现 `PSRAM ... failed` 时，应先确认
所烧录 profile 是否启用了 PSRAM，以及板卡是否确为带 PSRAM 的 N16R8 版本。

### 常用 profile

| Profile | 用途 | 适合何时使用 |
| --- | --- | --- |
| `audio_event_8class` | 本地 8-class、ESP-NN、OLED、LittleFS | 日常功能与性能演示主线。 |
| `audio_event_8class_remote_http` | 8-class + Wi-Fi/WAPI + HTTP 上报 + PSRAM arena | 局域网告警上报联调。需在本地配置接收端，勿提交凭据。 |
| `audio_event_8class_remote_pcm` | 在 Remote HTTP 基础上增加 UDP PCM streamer | 局域网 Dashboard 波形演示。 |
| `tflm_benchmark_s3_large_ref` | S3-large 的全 reference 模型基线 | 获取性能对照，不用于应用演示。 |
| `tflm_benchmark_s3_large_espnn_cycles` | S3-large 的完整 ESP-NN 性能 profile | 采集 100 次 Invoke 和每算子 CCOUNT。 |
| `tflm_benchmark_s3_large_espnn_verify` | S3-large ESP-NN 数值验证 | 仅验证；TRACE/VERIFY 会增加开销，不能当正式性能数据。 |

各 profile 的具体 `defconfig` 位于
[`board/esp32s3-devkit/configs/`](board/esp32s3-devkit/configs/)；板级接线与基础烧录流程见
[ESP32-S3 DevKit 配置说明](board/esp32s3-devkit/README.md)，更完整的配置/源码地图见
[目录架构说明](docs/目录架构说明.md)。SMP profile 是实验项，尚不应替代已验证的单核主线。

## 运行方式

### 真实麦克风

```text
nsh> audio_event --device /dev/audio/pcm_in1 --audio-stats --profile --no-oled
```

INMP441 当前按 `16 kHz, 2ch, 32-bit I2S` 采集，应用选用一个 slot 并右移转换为模型输入的
`16 kHz, mono, PCM16`。若 `audio_stats` 连续出现 `min=0 max=0 rms=0`，问题位于 I2S/DMA
输入或麦克风接线/供电，应先排查采集链路而不是模型或 ESP-NN。

### LittleFS 文件回放

主线 profile 为 16 MiB flash 的上半区保留 8 MiB LittleFS 资源分区，挂载点为 `/data`。
生成并烧录单个 WAV 资源镜像（例如 `combined_A_pure.wav`）的命令如下：

```bash
cd /path/to/openvela
MKLITTLEFS=/absolute/path/to/mklittlefs \
  ./ccf_audioevent/scripts/make_audio_event_littlefs_image.sh \
  ccf_audioevent/test_data/combined_A_pure.wav

esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash \
  0x800000 ccf_audioevent/out/audio_event_littlefs/audio_event_littlefs.bin
```

然后运行：

```text
nsh> ls /data
nsh> audio_event --file /data/combined_A_pure.wav --profile --no-oled
```

`--file` 是尽可能快的离线回放，不代表真实墙钟采集速率；实时性结论应以 `--device` 的 I2S
输入测试为准。文件找不到时会以 `status=-2` 退出，先用 `ls /data` 核对资源是否烧录成功。

### 能量门控

门控能力编译在支持它的 profile 中，但运行时默认关闭，保证连续推理基线可复现：

```text
nsh> audio_event --file /data/combined_A_pure.wav --profile --no-oled --no-power-gate
nsh> audio_event --file /data/combined_A_pure.wav --profile --no-oled --power-gate
```

前者是连续推理基线；后者启动时进行噪声地板标定，静音窗口可能跳过特征提取和推理。应同时记录
`infer/skip`、漏检/误报以及真实电流，不能只凭处理时间声称节电比例。

## ESP-NN 性能复现

性能 profile 不包含音频采集或 DSP，用于隔离模型内核速度。Reference 和 ESP-NN 应在相同供电、
频率和温度条件下连续测试。

```bash
cd /path/to/openvela
./ccf_audioevent/scripts/link_esp_nn.sh

# 1. Reference 基线
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_s3_large_ref -j8
# 烧录后：
# nsh> tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100

# 2. 完整 ESP-NN 性能
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_s3_large_espnn_cycles -j8
# 烧录后：
# nsh> tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
# nsh> tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

验证 profile 则运行：

```text
nsh> tflm_benchmark --mode invoke --input pattern --warmup 0 --repeat 1
```

关注每个已选节点的 `[espnn-verify] ... match bytes=...`，再使用无 TRACE/VERIFY 的性能
profile 采集正式数据。详细操作、节点掩码和历史结果见
[S3-large 8-class ESP-NN 优化操作手册](docs/项目基线/S3-large_8class_ESP-NN优化操作手册.md)。

## 可选：局域网告警与波形 Dashboard

HTTP 和 UDP 均为可选能力：HTTP 传输低频告警 JSON，UDP 传输可容忍少量丢包的 PCM 波形。
开始前，先在 NuttX 串口用 WAPI 完成 STA 联网，再确认设备可 `ping` 到上位机。SSID、密码、
接收端 IP、Token 均属于现场配置，不能写入提交或日志。

上位机启动接收端：

```bash
cd /path/to/openvela
python3 ccf_audioevent/scripts/remote_receiver.py \
  --host 0.0.0.0 --port 8080 --pcm-port 5004
```

浏览器打开 `http://<receiver-ip>:8080/`。使用已配置接收端地址的 Remote PCM 固件时，设备端可执行：

```text
nsh> audio_event --device /dev/audio/pcm_in1 --audio-stats --no-oled --pcm-stream
```

或用文件路径先验证协议和页面：

```text
nsh> audio_event --file /data/combined_A_pure.wav --profile --no-oled --pcm-stream
```

更多网络拓扑、安全边界、WAPI 配网、HTTP 数据契约和 PCM 包格式见
[远程告警上报方案](docs/复赛目标/远程告警上报方案.md)与
[实时 PCM 音频可视化方案](docs/复赛目标/实时PCM音频可视化方案.md)。

## 文档、日志与答辩材料

| 目标 | 文档 / 材料 |
| --- | --- |
| 从项目全貌开始 | [docs/README.md](docs/README.md) |
| 模型、特征、事件判决口径 | [项目基线/事件定义与触发口径说明](docs/项目基线/事件定义与触发口径说明.md) |
| 精度、误报/漏报与性能证据 | [项目基线/性能与评估](docs/项目基线/性能与评估.md) |
| S3-large 8-class 实测 | [模型测试/S3-large 8-class 端到端分类与告警测试](docs/模型测试/S3-large_8class_端到端分类与告警测试.md) |
| ESP-NN 接入、回退和验证 | [优化文档/ESP-NN 移植到 openvela 实施指南](docs/优化文档/ESP-NN移植到openvela实施指南.md) |
| 性能基线与复现实验 | [Reference 与 ESP-NN 性能对比](docs/复赛目标/renference与esp-nn对比.md) |
| 原始串口日志和命名规则 | [logs/readme.md](logs/readme.md) |
| 比赛成果和答辩问题 | [项目工作与成果总结](docs/比赛说明/项目工作与成果总结.md)、[评委问答](docs/比赛说明/评委问答.md) |

## 开源与第三方声明

项目自研源码与文档使用 [Apache License 2.0](LICENSE)。第三方依赖、ESP-NN 上游来源和
数据集许可摘要见 [NOTICE](NOTICE) 与 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
训练/数据集的许可边界与商用使用条件需要单独审查，不能仅凭本仓库的模型文件推定可商用。
