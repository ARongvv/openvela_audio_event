# 实时 PCM 音频可视化方案

> 状态：P1 已实现，待板端回归验证。  
> 目标：在可信局域网中，将 ESP32-S3 正在采集的 PCM 音频以低延迟方式发送到上位机，并在现有 Remote Alert Dashboard 中显示滚动波形；本地特征提取、推理和告警必须保持独立可用。

## 1. 结论与范围

不复用当前的 HTTP 告警队列传输音频。告警是低频、可重试的 JSON；PCM 是连续、高频、允许少量丢包的实时数据，两者的时延、队列和可靠性要求相反。

首版使用 **UDP PCM packet → 上位机 Python receiver → 浏览器 Canvas**：

```text
I2S / WAV PCM block
        │
        ├──> 既有 ring buffer → 特征提取 → TFLM / ESP-NN → HTTP 告警
        │
        └──> 有界 PCM 队列 → UDP streamer 线程 → 上位机 UDP 接收线程
                                                   │
                                                   └──> Dashboard 滚动波形
```

首版只显示波形和传输统计，不在浏览器播放远端声音、不保存 PCM 到磁盘、不传输到公网。

## 2. 为什么选择 UDP

当前输入为 16 kHz、单声道、PCM16：

```text
16000 samples/s × 2 B/sample = 32000 B/s ≈ 31.25 KiB/s
```

应用当前每次最多处理 `512` 个采样，即 `32 ms`、`1024 B`。一帧加上小型协议头约 1050 B，低于常见 1500 B LAN MTU，约 31 包/s。

| 方式 | 结论 |
| --- | --- |
| 每块 HTTP POST | 不采用；约 31 次连接/请求每秒，协议与服务端开销过大。 |
| TCP 长连接 | 暂不采用；重传和队头阻塞会放大网络抖动，可能拖慢后台线程。 |
| WebSocket（设备直连） | 暂不采用；需要额外客户端状态机、握手与重连实现。 |
| UDP | 首选；一包丢失只形成约 32 ms 波形缺口，适合可信 LAN 监看。 |

浏览器不能直接接收 UDP。因此上位机接收端将 UDP 数据写入内存中的短期采样窗口，由网页轮询 JSON 接口并通过 Canvas 绘制。

## 3. 数据包协议

使用网络字节序的固定头，后随原始 little-endian PCM16 payload：

```text
0               4               8              16              20
+---------------+---------------+---------------+---------------+
| magic "AEPC"  | version = 1   | sequence      | timestamp_ms  |
+---------------+---------------+---------------+---------------+
| sample_count  | flags/reserved| PCM16 samples ...              |
+---------------+---------------+---------------+---------------+
```

建议字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `magic` | `uint32_t` | 固定值，过滤非本协议 UDP 数据。 |
| `version` | `uint16_t` | 首版为 `1`。 |
| `sequence` | `uint32_t` | 单调递增，用于检测网络丢包与乱序。 |
| `timestamp_ms` | `uint64_t` | 自音频开始的采样时间，和告警 JSON 的 `monotonic_ms` 对齐。 |
| `sample_count` | `uint16_t` | 首版最大 512。 |
| `flags` | `uint16_t` | 预留。 |
| `payload` | `int16_t[]` | 单声道 16 kHz PCM，小端。 |

接收端校验 magic、version、长度和 `sample_count`，异常包直接丢弃并计数。`sequence` 不连续仅记录 `network_lost_packets`，不得要求设备重发。

## 4. 设备端设计

### 4.1 线程与优先级

| 线程 | 职责 | 约束 |
| --- | --- | --- |
| capture worker | I2S 读 PCM、更新 ring buffer | 最高优先级，绝不做网络 I/O。 |
| `audio_event` 主线程 | 特征提取、TFLM、检测与本地告警 | 推理路径不等待 streamer。 |
| HTTP reporter | 发送低频告警 JSON | 使用既有有界队列。 |
| 新增 PCM streamer | 从 PCM 队列取帧、`sendto()` UDP | 后台线程；网络慢时允许丢 PCM。 |

单核首版中，PCM streamer 不能高于 capture worker。启用 SMP 后，推荐将采集/特征/推理固定到 CPU0，将 Wi-Fi、HTTP reporter 和 PCM streamer 固定到 CPU1；但 SMP/affinity 必须作为独立 profile 和独立验证项，不能与首版 PCM 协议改动混在同一次验证中。

### 4.2 有界队列

`pcm_streamer_submit()` 的职责只包括复制一个 PCM block 至预分配队列并唤醒工作线程：

- 队列深度建议初始为 8 帧，即约 8 KiB PCM payload；
- 8-class/Wi-Fi 配置内部 DRAM紧张，帧存储放在既有 PSRAM 音频工作区预留
  的尾部；不为约 8 KiB 队列再发起独立 heap 申请；
- 队列满时丢弃最新帧并递增 `queue_dropped_frames`；
- 禁止 `malloc`、DNS、socket、锁等待或重传出现在 capture worker 中；
- streamer 线程内部可使用固定约 1.1 KiB 发送缓冲；
- 停止时丢弃尚未发送帧并打印统计，不延迟应用退出。

### 4.3 输入路径接入点

两种输入都必须覆盖：

| 输入 | 当前 PCM 获得位置 | 新增调用位置 |
| --- | --- | --- |
| 麦克风 | `capture_thread_main()` 中的 `audio_capture_read()` | `ring_append()` 前调用 `pcm_streamer_submit()`。 |
| WAV 文件 | `audio_event_main()` 中的 `audio_file_read()` | `ring_append()` 前调用 `pcm_streamer_submit()`。 |

File 模式是尽可能快的离线回放，远端波形会随之加速；它适合协议、丢包与可视化验证。只有 I2S 采集模式才代表真实墙钟时间。

## 5. 上位机 Dashboard 设计

在 [`scripts/remote_receiver.py`](../../scripts/remote_receiver.py) 中新增 UDP 接收线程，保留既有 HTTP POST 接口不变：

| URL / 端口 | 作用 |
| --- | --- |
| `POST /api/v1/audio-events` | 保持现有告警上报兼容。 |
| `GET /api/v1/events` | 保持既有告警仪表盘查询。 |
| `UDP 5004`（建议默认） | 接收实时 PCM packet。 |
| `GET /api/v1/audio-window` | 返回最近 3~5 秒的下采样波形与传输统计。 |
| `GET /` | Dashboard Canvas 绘制波形，并叠加按 `timestamp_ms` 对齐的告警标记。 |

网页只使用浏览器原生 Canvas 和 Fetch，不依赖 npm、Flask、数据库或外部 CDN。为控制页面负载，`/api/v1/audio-window` 应返回下采样后的 min/max 包络，而不是完整 PCM。

## 6. 计划修改文件

| 文件 | 修改内容 |
| --- | --- |
| `app/audio_event/Kconfig` | 新增 PCM stream 开关、服务器地址、UDP 端口、队列深度、线程栈与优先级。默认关闭。 |
| `app/audio_event/Makefile` | 增加 streamer C 源文件。 |
| `app/audio_event/CMakeLists.txt` | 增加 streamer C 源文件。 |
| 新增 `app/audio_event/streamer/pcm_streamer.h` | 定义 init、submit、deinit、stats 接口。 |
| 新增 `app/audio_event/streamer/pcm_streamer.c` | 实现预分配队列、后台 UDP 发送和统计。 |
| `app/audio_event/main/audio_event_main.c` | 在 File 与 I2S 的 PCM 分叉点调用 streamer；处理初始化和退出。 |
| 新增 `board/.../audio_event_8class_remote_pcm/defconfig` | 从 Remote HTTP profile 派生，显式启用 PCM stream；不改动已验证 profile。 |
| `scripts/remote_receiver.py` | 增加 UDP 接收、audio-window API 与 Canvas 波形组件。 |
| `docs/复赛目标/远程告警上报方案.md` | 链接本文档并补充首版范围说明。 |

## 7. Kconfig 建议

```text
CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM=n
CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_HOST=""
CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_PORT=5004
CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_QUEUE_DEPTH=8
CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_STACKSIZE=6144
CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_PRIORITY=101
```

host 保持空字符串时，初始化失败并明确打印原因；不得将现场局域网地址、SSID、密码或 Token 提交到仓库。首版通过运行时 `--pcm-stream` 开关启用，避免设备默认传输原始语音。

## 8. 当前实现与运行方式

已新增 `audio_event_8class_remote_pcm` profile。它基于已验证的单核 Remote
HTTP profile，启用 UDP、PSRAM PCM 队列与 Dashboard 接收能力；不依赖当前尚未
启动成功的 SMP profile。PCM 队列不是小块动态申请：应用在已验证的 PSRAM 音频
工作区末尾额外预留 `queue_depth × 512 × PCM16` 帧存储，并将该切片交给
streamer。这避免 NuttX 小块分配优先命中内部 DRAM 而导致的 `-ENOMEM` 误判。
上位机不会保存原始 PCM：每个 32 ms UDP 包会细分为 16 个 2 ms 的 min/max
包络点，浏览器以 10 Hz 增量拉取最近 5 秒时间轴并使用 Canvas 连续绘制。

在本地 `menuconfig` 中配置以下两个地址（不要提交）：

```text
HTTP endpoint: http://<receiver_ip>:8080/api/v1/audio-events
PCM stream receiver IPv4 address: <receiver_ip>
PCM stream receiver UDP port: 5004
```

构建后，上位机先启动：

```sh
cd ~/openvela
python3 ccf_audioevent/scripts/remote_receiver.py --host 0.0.0.0 --port 8080 --pcm-port 5004
```

浏览器打开 `http://<receiver_ip>:8080/`，设备端使用：

```sh
audio_event --file /data/combined_A_pure.wav --profile --no-oled --pcm-stream
```

启动日志应显示 `[pcm] udp enabled ... psram=1`；结束时的 `sent`、
`queue_dropped` 和 `send_dropped` 用于评估是否影响音频主路径。

## 9. 分阶段实施与验收

### 阶段 P0：上位机本地波形

Dashboard 支持选择本地 WAV、Canvas 绘制波形并按告警 JSON 的 `monotonic_ms` 标记事件。无固件改动，用于先验证展示交互。

### 阶段 P1：设备 UDP PCM

实现设备队列与 UDP 接收；只显示滚动波形与 packet loss。验收：连续 10 分钟 I2S 输入，`queue_dropped_frames=0`，上位机解析错误为 0，且推理仍可稳定告警。

### 阶段 P2：性能与鲁棒性

比较 PCM stream 开关前后的 `feature`、`infer`、总处理时间、告警漏检/误报和 Wi-Fi 丢包。若 streamer 导致单核实时性恶化，再单独引入 SMP profile 与 affinity 验证。

## 10. 风险与边界

| 风险 | 缓解方式 |
| --- | --- |
| 网络抖动或断网 | UDP 可丢帧；队列有界；不重传；推理路径独立。 |
| Wi-Fi 占用 CPU / DRAM | 后台线程、PSRAM 队列、先做单核性能回归，再评估 SMP。 |
| File 回放并非真实时间 | 明确其只用于协议测试；实时结论只来自 I2S。 |
| 隐私泄露 | 默认关闭、可信 LAN、无磁盘持久化、无公网暴露。 |
| 浏览器负载 | 仅返回短窗口和下采样包络，Canvas 绘制限制刷新率。 |

## 11. 不在首版范围内

- WebRTC、浏览器实时音频播放、设备直连 WebSocket；
- TLS/DTLS、跨公网传输、账号鉴权和云端存储；
- PCM 长期录音、回放检索与数据集采集；
- 将 TFLM 单次 `Invoke()` 拆分到两个 CPU 并行执行。
