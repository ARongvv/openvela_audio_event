# Audio Event 模型校准与 ESP32-S3-BOX 移植指南

> 版本：1.0  
> 更新日期：2026-06-23  
> 适用目录：`apps/examples/audio_event`  
> 实施原则：先证明数值正确，再迁移驱动；先录音验收，再运行模型

## 1. 文档目标

本文档解决两个当前最重要的问题：

1. 修复真实安静环境被持续识别为 `knock` 的模型问题。
2. 将已经在 NuttX Native Sim 上运行的 `audio_event` 移植到
   ESP32-S3-BOX 或 ESP32-S3-BOX-3。

本文档不把提高阈值视为模型修复。阈值只能控制告警频率，不能修复错误的
特征分布、训练数据或类别边界。

## 2. 已验证基线

### 2.1 模拟器链路

当前 Native Sim 已经完成以下闭环：

```text
Linux 麦克风
  -> ALSA default capture
  -> NuttX Sim Audio Driver
  -> /dev/audio/pcm0c
  -> 16 kHz mono PCM16
  -> 49x40 log-mel
  -> TFLite Micro INT8 inference
  -> detector
  -> serial alert
```

Sim 中已确认存在以下设备：

```text
/dev/audio/pcm0c
/dev/audio/pcm0p
/dev/audio/pcm1c
/dev/audio/pcm1p
/dev/audio/mixer
```

### 2.2 当前异常

一段全部为安静环境的日志包含 159 次推理，统计结果为：

| 预测类别 | 次数 | 比例 |
|---|---:|---:|
| knock | 136 | 85.5% |
| cough | 14 | 8.8% |
| background | 9 | 5.7% |
| silence | 0 | 0% |

平均输出概率约为：

```text
knock=610, cough=229, background=158, silence=3
```

结论：音频采集、推理和告警状态机已经运行，但模型在真实麦克风输入上存在
严重分布偏移。禁止将当前结果作为板端准确率基线。

### 2.3 固件模型契约

`model/metadata.json`、训练脚本和固件常量当前一致：

| 项目 | 值 |
|---|---|
| 采样率 | 16000 Hz |
| 音频窗口 | 1000 ms，16000 samples |
| STFT 窗口 | 30 ms，480 samples |
| STFT 帧移 | 20 ms，320 samples |
| FFT | 512 points |
| Mel 通道 | 40 |
| 特征 shape | `[49, 40, 1]` |
| 展平特征 | 1960 floats |
| 输入 | INT8 `[1,1960]` |
| 输出 | INT8 `[1,4]` |
| 类别顺序 | knock, cough, background, silence |

维度一致并不代表数值一致。当前首要任务是比较 Python 和 C 端对同一个 PCM
窗口生成的 1960 个特征。

## 3. 总体实施顺序

按照以下关卡推进。上一关未通过时，不将下一关结果作为有效结论。

| 关卡 | 目标 | 完成标志 |
|---|---|---|
| M0 | 固定 PCM 的 Python 基准 | 保存特征、INT8 输入和概率 |
| M1 | C/Python 特征对齐 | 量化输入基本一致 |
| M2 | 真实麦克风数据集 | 安静、背景、knock、cough 均有标注 |
| M3 | 重新训练与阈值扫描 | 真实设备验证集达到目标指标 |
| H0 | BOX 基础系统 | NSH、PSRAM、TFLM 应用可启动 |
| H1 | BOX 麦克风驱动 | `nxrecorder` 可获得正确 PCM |
| H2 | BOX 文件推理 | 固定 PCM 与 Sim 输出一致 |
| H3 | BOX 实时推理 | 麦克风到告警闭环稳定运行 |
| H4 | 板端验收 | RAM、延迟、误报和漏报达标 |

模型校准 M0-M3 与板级驱动 H0-H1 可以并行进行。H2 之后必须使用已通过
M3 的模型。

## 4. 模型问题定位

### 4.1 建立单一测试输入

从训练集或真实录音中选择一个明确标注的 1 秒 PCM/WAV，例如：

```text
testdata/golden/background_001.wav
```

要求：

- 16000 Hz。
- 单声道。
- signed PCM16。
- 正好 16000 samples。
- 不做每文件峰值归一化，除非训练和部署都采用同一规则。

同一文件必须分别进入 Python 和固件，不能用两次独立录音做数值比较。

### 4.2 保存 Python 黄金结果

建议为每个黄金样本保存以下文件：

```text
golden/background_001/
├── audio.pcm
├── feature.f32        # 1960 个 float32，小端序
├── input.i8           # 按模型 scale/zp 量化后的 1960 bytes
├── output.i8          # 模型原始 INT8 输出
└── result.json        # 类别、反量化概率和模型哈希
```

Python 前处理必须沿用训练脚本中的参数：

```text
tf.signal.stft(..., frame_length=480, frame_step=320, fft_length=512)
magnitude = abs(stft)
mel = magnitude @ linear_to_mel_weight_matrix(40, 257, 16000, 125, 7500)
feature = clip((log(mel + 1e-6) + 12.0) * 1.625, 0, 26)
```

### 4.3 保存固件结果

为文件测试模式增加可关闭的诊断输出：

```text
--dump-feature /host/feature.f32
--dump-input /host/input.i8
```

诊断功能只用于开发构建。生产构建不需要持续写文件。

比较以下指标：

| 指标 | 要求 |
|---|---|
| shape | 必须是 1960 |
| NaN/Inf | 必须为 0 |
| 特征平均绝对误差 | 记录并定位系统偏移 |
| 特征最大绝对误差 | 定位单频带异常 |
| INT8 不同元素比例 | 作为主要验收指标 |
| 输出差异 | 每类不超过约 1 个输出量化步长为优 |
| 最终类别 | 必须一致 |

不要只比较最终类别。大量特征偏差可能暂时没有改变某个样本的 argmax，但会在
真实环境中造成严重误报。

### 4.4 分支判断

```text
Python 正确，C 错误
  -> 修复 C 端 Hann、FFT、Mel、log、展平或量化

Python 和 C 都错误
  -> 模型/数据集问题，重新训练

Python/C 特征一致，但 TFLM 输出不同
  -> 检查模型文件哈希、输入 scale/zp、resolver 和输出反量化
```

### 4.5 重点检查项

按以下顺序检查 `dsp/feature_extract.c`：

1. WAV header 是否已被正确跳过。
2. PCM 是否按 `sample / 32768.0f` 转换。
3. Hann 是否为 TensorFlow 默认 periodic window。
4. FFT 是否未做额外归一化。
5. 使用 magnitude，而不是 power spectrum。
6. Mel 频率上下限是否为 125 Hz 和 7500 Hz。
7. Mel 三角权重是否与 TensorFlow 定义一致。
8. DC bin 是否按 TensorFlow Mel matrix 处理。
9. `log(mel + 1e-6)` 是否使用自然对数。
10. 特征缩放、clip 和展平顺序是否一致。

## 5. 真实麦克风数据与重新训练

### 5.1 为什么现有 silence 不够

训练脚本中的 silence 主要由全零和小幅高斯噪声生成。真实麦克风还包含：

- 模拟或数字前端底噪。
- 自动增益和固定增益。
- USB/ALSA 或 codec 的频率响应。
- 风扇、空调、电源和桌面低频振动。
- 房间混响与远处人声。

因此真实安静环境不能只依赖合成零样本建模。

### 5.2 建议采集规模

| 类别 | 最低建议 |
|---|---:|
| 安静环境 | 30-60 分钟，多个房间 |
| 普通背景 | 30-60 分钟，风扇、键盘、说话、音乐 |
| knock | 每种材质/距离至少 200 次 |
| cough | 多人，每人至少 50 次 |

采集时保存设备、增益、距离、环境和事件时间。训练、验证和测试必须按原始录音
分组拆分，禁止同一长录音切出的相邻窗口跨越数据集分区。

### 5.3 训练策略

- 继续使用事件中心化的 1 秒切片。
- background 包含真实安静、普通室内噪声和容易混淆的瞬态声音。
- silence 可以保留，但必须混入真实设备噪声，不能只用数字零。
- 增益增强范围应覆盖目标板麦克风的真实幅度。
- 添加不同 SNR 的背景混合，但验证集不做随机增强。
- 保持训练和部署都不进行单文件峰值归一化，或两端都采用同一算法。

### 5.4 阈值策略

阈值必须在真实设备验证集上重新扫描。当前安静日志中 knock 经常达到
`600-720/1000`，因此离线得到的较低阈值不能直接部署。

建议检测器增加释放条件：

```text
armed
  -> 连续 N 帧高于触发阈值 -> alert
  -> disarmed
  -> 目标分数低于释放阈值，或 background 连续胜出 M 帧
  -> armed
```

冷却时间用于限制告警频率，释放条件用于防止持续偏高的类别反复告警，两者不能
互相替代。

## 6. ESP32-S3-BOX 仓库现状

仓库已经包含：

```text
nuttx/boards/xtensa/esp32s3/esp32s3-box/
vendor/espressif/boards/esp32s3/esp32s3-box/configs/openvela/
```

现有 BOX BSP 支持 LCD、触摸、Wi-Fi、PSRAM 和 NSH，但
`esp32s3_bringup()` 中没有音频 codec 或 I2S 麦克风初始化，现有 defconfig 也没有
启用音频。

仓库已有可参考的 ESP32-S3 音频实现：

```text
nuttx/boards/xtensa/esp32s3/esp32s3-korvo-2/
nuttx/boards/xtensa/esp32s3/common/src/esp32s3_es8311.c
nuttx/drivers/audio/es8311.c
```

这些代码只能作为结构模板，不能复制 Korvo-2 的引脚到 BOX。

## 7. 上板前确认硬件版本

必须先确认以下信息：

- 板卡是 ESP32-S3-BOX、BOX-Lite 还是 BOX-3。
- 模组 Flash 和 PSRAM 容量。
- 麦克风/音频 ADC 型号。
- I2C 地址与 I2C 引脚。
- I2S BCLK、WS、DIN、DOUT、MCLK 引脚。
- 麦克风通道数和 TDM/I2S 数据格式。
- codec 支持的采样率。
- 功放使能、codec reset 和电源控制 GPIO。

如果实际麦克风 ADC 没有 NuttX 驱动，需要实现新的 Audio lower-half。不要假设
现有 ES8311 驱动一定对应板载麦克风。

## 8. 板级音频移植

### 8.1 目标设备接口

建议硬件最终注册标准 NuttX Audio 输入设备：

```text
/dev/audio/pcm_in0
```

这样可以继续复用当前 `audio/audio_capture.c`，只需修改运行参数或 Kconfig
默认设备路径：

```text
audio_event --device /dev/audio/pcm_in0
```

Sim 使用 `/dev/audio/pcm0c`，真实板设备名由板级驱动注册逻辑决定。

### 8.2 驱动实现路径

如果硬件 codec 已有驱动：

1. 配置 I2C 和 I2S 引脚。
2. 初始化 ESP32-S3 I2S bus。
3. 初始化 codec input lower-half。
4. 使用 `audio_register()` 注册输入设备。
5. 在 `esp32s3_bringup()` 中调用初始化函数。

如果硬件 codec 没有驱动：

1. 实现 codec I2C 寄存器配置。
2. 实现 NuttX `audio_lowerhalf_s` 操作集。
3. 对接 ESP32-S3 I2S RX 和 DMA。
4. 实现 reserve/configure/start/stop/enqueue/dequeue/release。
5. 注册 `/dev/audio/pcm_in0`。
6. 先通过 `nxrecorder`，再运行 `audio_event`。

### 8.3 音频格式适配

模型要求：

```text
16000 Hz, mono, signed PCM16
```

板载 codec 可能输出 48 kHz、双通道或 TDM 数据。驱动或应用输入层必须明确完成：

- 选择一个麦克风通道或执行下混。
- 48 kHz 到 16 kHz 的抗混叠降采样。
- 24/32-bit sample 到 PCM16 的饱和转换。
- 去除 TDM 空槽和通道交织。

禁止简单丢弃每三个采样点中的两个来代替抗混叠降采样。

## 9. 专用 defconfig

不要直接把项目配置混入通用 `openvela/defconfig`。建议创建：

```text
vendor/espressif/boards/esp32s3/esp32s3-box/configs/audio_event/defconfig
```

从 BOX 的 `openvela/defconfig` 或精简 `nsh/defconfig` 派生，并检查以下配置组：

```text
CONFIG_ARCH_BOARD_ESP32S3_BOX=y
CONFIG_ESP32S3_SPIRAM=y
CONFIG_HAVE_CXX=y
CONFIG_HAVE_CXXINITIALIZE=y

CONFIG_AUDIO=y
CONFIG_DRIVERS_AUDIO=y
CONFIG_AUDIO_I2S=y
CONFIG_ESP32S3_I2S=y
CONFIG_ESP32S3_I2S0=y
CONFIG_ESP32S3_I2C0=y

CONFIG_TFLITEMICRO=y
CONFIG_MATH_KISSFFT=y
CONFIG_EXAMPLES_AUDIO_EVENT=y
CONFIG_EXAMPLES_AUDIO_EVENT_DEVPATH="/dev/audio/pcm_in0"
```

codec、I2S 引脚和采样格式配置必须来自实际板卡原理图。硬件配置中关闭
`CONFIG_SIM_SOUND`，也不需要 Sim 的 MAD/LAME 依赖。

## 10. RAM 与存储优化

### 10.1 当前静态内存估算

当前主要静态数组约占：

| 缓冲区 | 大小 |
|---|---:|
| Tensor arena | 98304 bytes |
| audio ring | 32000 bytes |
| audio window | 32000 bytes |
| feature float buffer | 7840 bytes |
| audio block | 1024 bytes |
| FFT state | 8192 bytes |
| Hann/FFT/Mel 辅助缓冲 | 约 7 KB |

合计约 184 KiB，尚未包括：

- 音频驱动 DMA buffers。
- 应用 16 KiB 栈。
- TFLM 对象和 libc/C++ 运行时。
- NuttX 内核、文件系统、网络和显示。

### 10.2 优化顺序

1. 打印并记录 `arena_used_bytes()`，逐步缩小 96 KiB arena。
2. 合并 `audio_ring` 与 `audio_window`，避免两个完整 1 秒 PCM 缓冲。
3. 特征计算完成后直接量化，评估取消 1960-float 常驻缓冲。
4. 将 tensor arena、模型工作区等非 DMA 大缓冲放入 PSRAM。
5. I2S DMA descriptor 和 DMA buffers 保留在内部可 DMA RAM。
6. 使用 `size`、map 文件、运行期 heap/stack watermark 记录真实占用。

不要仅因配置了 PSRAM 就认为所有 `.bss` 会自动放到外部 RAM。必须通过链接 map
或运行地址确认缓冲区所在内存区域。

## 11. 构建、烧录与启动

### 11.1 构建

在仓库根目录执行：

```bash
source build/envsetup.sh
./build.sh vendor/espressif/boards/esp32s3/esp32s3-box/configs/audio_event/ -j8
```

首次建立配置后，使用 `make menuconfig` 调整，再执行：

```bash
cd nuttx
make savedefconfig
```

将生成的最小 defconfig 保存到项目专用配置目录。

### 11.2 烧录

确认串口：

```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

烧录示例：

```bash
cd nuttx
make -j8 flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BINDIR=./
```

串口连接：

```bash
minicom -D /dev/ttyACM0 -b 115200

picocom -b 115200 /dev/ttyACM0
```

## 12. 分层验收

### 12.1 H0：系统和应用

```text
nsh> help | grep audio_event
nsh> free
nsh> dmesg
```

验收：系统无启动异常，PSRAM 初始化成功，`audio_event` 已注册。

### 12.2 H1：麦克风驱动

```text
nsh> ls /dev/audio
nsh> nxrecorder
```

录制至少 10 秒 PCM，并在桌面检查：

- 文件长度正确。
- 通道顺序正确。
- 无持续全零、全满幅或明显 DMA 断裂。
- 实际采样率与声明一致。
- 安静 RMS、峰值和直流偏置处于合理范围。

### 12.3 H2：固定文件推理

将 Sim 已验证的黄金 PCM 放入板端文件系统：

```text
nsh> audio_event --file /data/golden/background.wav --once
```

验收：与 Sim 的量化输入和输出概率一致或处于已定义容差内。

### 12.4 H3：实时推理

```text
nsh> audio_event --device /dev/audio/pcm_in0
```

按固定脚本测试安静、背景、knock 和 cough，并保存时间标注与完整概率日志。

### 12.5 H4：稳定性与性能

至少记录：

- DSP 时间。
- TFLM Invoke 时间。
- 端到端告警延迟。
- 峰值 RAM、剩余 heap 和栈水位。
- 固件 Flash 增量。
- 连续运行 30 分钟稳定性。
- 安静误报率、事件漏报率和每小时误报次数。

## 13. 最终验收标准

- [ ] 同一 PCM 的 Python/C 特征通过数值对齐。
- [ ] Python TFLite 与板端 TFLM 输出通过量化容差。
- [ ] 真实安静数据不再长期偏向 knock。
- [ ] 阈值来自真实设备验证集，而不是训练集准确率。
- [ ] BOX 音频设备可被 `nxrecorder` 独立验证。
- [ ] 板端输入严格为 16 kHz mono PCM16。
- [ ] 固定文件在 Sim 与 BOX 上得到一致结果。
- [ ] DMA buffers 位于内部 RAM，大工作区按设计使用 PSRAM。
- [ ] 连续运行 30 分钟无崩溃、死锁和 buffer starvation。
- [ ] 报告包含准确率、误报率、漏报率、延迟、RAM、Flash 和功耗来源。

## 14. 推荐下一步

当前最小闭环不是立即烧录模型，而是同时完成两项工作：

1. 为 `audio_event` 增加特征和量化输入导出，完成第一个 golden-vector 对比。
2. 确认 BOX 具体版本、音频 ADC 型号和 I2S/I2C 引脚，建立专用
   `audio_event/defconfig`，先让 `nxrecorder` 在板端工作。

这两项通过后，模型校准和硬件驱动的边界就会清晰，后续优化才有可信的测量基础。
