# S3-large 8-class ESP-NN 优化操作手册

> 状态：代码与 profile 已接入，等待 ESP32-S3 真机构建、烧录和逐字节验证。
>
> 模型来源：`~/Documents/audio/micro_model/output/s3_large_8class/model_int8.tflite`
>
> 模型 SHA-256：`af4cb7d74615fd1a57b0d0a4ace5032d2abb7a2ec5a8cf31119bcbb67a668ab0`

## 1. 模型与算子清单

模型为全 INT8 DS-CNN，输入 `[1,5880]`，输出 `[1,8]`，模型文件 37,784 B。类别顺序为：

```text
knock, cough, glass_breaking, yes, no, stop, background, silence
```

TFLite 子图有 14 个算子、37 个 tensor：

| op | 算子 | 输入/输出形状 | 输出 tensor ID | ESP-NN 策略 |
| ---: | --- | --- | ---: | --- |
| 0–3 | Shape/StridedSlice/Pack/Reshape | `[1,5880] -> [1,49,40,3]` | 23–26 | TFLM reference |
| 4 | Conv2D 5×5, s2, 3→32 | `[1,49,40,3] -> [1,25,20,32]` | 27 | ESP-NN general/im2col |
| 5 | DWConv 3×3, C=32 | `[1,25,20,32]` | 28 | ESP-NN S3 padded s8 |
| 6 | Conv2D 1×1, 32→64 | `[1,25,20,32] -> [1,25,20,64]` | 29 | ESP-NN mult8 1×1 |
| 7 | DWConv 3×3, C=64 | `[1,25,20,64]` | 30 | ESP-NN S3 padded s8 |
| 8 | Conv2D 1×1, 64→64 | `[1,25,20,64]` | 31 | ESP-NN mult8 1×1 |
| 9 | DWConv 3×3, C=64 | `[1,25,20,64]` | 32 | ESP-NN S3 padded s8 |
| 10 | Conv2D 1×1, 64→96 | `[1,25,20,64] -> [1,25,20,96]` | 33 | ESP-NN mult8 1×1 |
| 11 | Mean axes `{1,2}` | `[1,25,20,96] -> [1,96]` | 34 | ESP-NN NHWC int8 Mean |
| 12–13 | FullyConnected/Softmax | `[1,96] -> [1,8]` | 35–36 | TFLM reference |

没有修改 `third_party/esp-nn` 或 `apps/mlearning/esp-nn` 源码。改动只发生在 TFLM dispatcher：

- Depthwise 白名单由 12/16 通道增加 32/64 通道；
- Mean 白名单由 24 通道增加 96 通道；
- 每个 Conv/DW 节点仍必须由 tensor ID 显式选择；
- verify 发现不匹配时恢复 reference 输出，避免错误继续传播。

## 2. 三套 profile

| profile | Arena | TRACE/VERIFY | 用途 |
| --- | ---: | --- | --- |
| `tflm_benchmark_s3_large_ref` | 196,608 B | 关闭 | 所有算子走 reference，建立公平基线 |
| `tflm_benchmark_s3_large_espnn_verify` | 262,144 B | 全部开启 | 四 Conv、三 DW、Mean96 逐字节组合验证 |
| `tflm_benchmark_s3_large_espnn_cycles` | 196,608 B | 关闭 | 同一全组合的正式 CCOUNT/算子性能 |

reference profile 仍链接 ESP-NN wrapper，但 Conv/DW mask 为默认空且 Mean 未启用，因此不会选择加速节点。
这样 reference 和 ESP-NN 固件共享相同 TFLM 接口、编译优化和 CCOUNT 口径。

全组合配置为：

```text
Conv output 27/29/31 mask = 0xa8000000
Conv output 33 single ID  = 33
DW output 28/30 mask      = 0x50000000
DW output 32 single ID    = 32
Mean output 34            = CONFIG_TFLITEMICRO_ESP_NN_MEAN=y
```

mask 只能表示 tensor ID 0–31，因此 output 32/33 必须使用单 tensor ID 配置，不能写入 32-bit mask。

## 3. Arena 预算

当前 4-class 模型的 64 KiB arena 不适用于 S3-large。主要工作集包括：

| 项目 | 大小 |
| --- | ---: |
| 末级激活 `[1,25,20,96]` | 48,000 B |
| C=64 DW 的完整 padded-input scratch | 38,016 B |
| C=64 DW filter 与对齐余量 | 592 B |
| 最大 Conv 1×1 权重 | 6,144 B |
| verify 最大 reference output | 48,000 B |

此外还有相邻激活、per-channel multiplier/shift、TFLM planner 元数据以及其他 persistent/scratch buffer。
因此 reference/performance 从 192 KiB 起步，verify 使用 Kconfig 当前上限 256 KiB。真机输出中的
`arena_used` 才是最终依据：

- `AllocateTensors failed`：说明 arena 不足，先记录失败值；performance 也提高至 256 KiB 再复测；
- 链接阶段 `.bss`/DRAM 溢出：这是静态 arena 放置问题，不是 TFLM planner 问题，需要将 benchmark arena
  改为从启用的 SPIRAM common heap 进行 16-byte 对齐分配；
- verify 通过后，按 `arena_used + 安全余量` 收缩 performance arena，不要先按估算值压缩。

## 4. 构建与烧录顺序

构建和烧录由开发者执行。每次切换 profile 都重新配置、构建和烧录，不复用上一 profile 的性能结论。

### 4.1 Reference 基线

```sh
cd ~/openvela
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_s3_large_ref -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

板端先运行一次预检：

```sh
tflm_benchmark --mode invoke --input pattern --warmup 0 --repeat 1
```

应看到模型大小 37,784 B、输入 5,880 元素、实际 `arena_used` 和稳定的 8-byte 输出 hash。然后采集正式基线：

```sh
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

保存完整日志，不只保存最后一行。

### 4.2 全组合逐字节验证

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_s3_large_espnn_verify -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

只运行一次，避免 TRACE/VERIFY 串口日志和双执行干扰性能：

```sh
tflm_benchmark --mode invoke --input pattern --warmup 0 --repeat 1
```

必须看到所有八个加速节点均被选中并逐字节匹配：

```text
[espnn-verify] Conv2D out_t=27 match bytes=16000
[espnn-verify] DepthwiseConv2D out_t=28 match bytes=16000
[espnn-verify] Conv2D out_t=29 match bytes=32000
[espnn-verify] DepthwiseConv2D out_t=30 match bytes=32000
[espnn-verify] Conv2D out_t=31 match bytes=32000
[espnn-verify] DepthwiseConv2D out_t=32 match bytes=32000
[espnn-verify] Conv2D out_t=33 match bytes=48000
[espnn-verify] Mean out_t=34 match bytes=96
```

同时检查每个节点为 `candidate=1 selected=1 reason=ok` 和 `backend=esp-nn`。任一 mismatch、重启、
alignment/reference 回退都表示组合尚未通过；verify 固件的 Invoke 时间不能作为性能结果。

### 4.3 正式 ESP-NN 性能

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_s3_large_espnn_cycles -j8
make -C nuttx flash ESPTOOL_PORT=/dev/ttyUSB0
```

```sh
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

正式比较时要求：

1. reference 与 ESP-NN 的 `output_hash` 完全一致；
2. 两者使用相同 240 MHz CCOUNT 口径、输入 pattern、warmup/repeat；
3. 报告 min/P50/mean/P95/max，不用单次结果代表性能；
4. 单独列出 Conv、DW、Mean、FC/Softmax 的 operator cycles；
5. 记录 `arena_used`、固件 revision、模型 SHA 和 profile 名称。

## 5. 验收与后续接入

完成以下条件后，才能把 S3-large 接入生产 `audio_event`：

- [ ] reference profile 可稳定 AllocateTensors/Invoke；
- [ ] 四 Conv、三 DW、Mean96 全部逐字节 match；
- [ ] reference 与正式 ESP-NN 的 100 次 `output_hash` 一致；
- [ ] ESP-NN Invoke P95 加上当前 60–70 ms 特征提取后仍显著低于 250 ms hop；
- [ ] arena、静态 RAM、任务栈和连续运行没有异常；
- [ ] 8-class 类别映射、检测阈值、UI/告警/上报已从固定 4-class 逻辑完成适配；
- [ ] 使用真实 WAV 和麦克风完成误报率、漏报率与不同 SNR/距离回归。

benchmark 通过只证明模型内核可用，不等于生产 8-class 应用已经完成。生产接入仍需调整
`AUDIO_EVENT_CLASS_COUNT`、类别表、detector 阈值和应用输出契约。
