# audio_test 采集诊断工具

`audio_test` 只检查音频采集，不加载模型、不跑 OLED UI。它适合排查硬件接线、I2S slot、
位宽转换和削波问题。

## 常用命令

默认测试：

```text
nsh> audio_test --device /dev/audio/pcm_in1 --seconds 5
```

显式指定 2 声道诊断：

```text
nsh> audio_test --device /dev/audio/pcm_in1 --channels 2 --seconds 5
```

## INMP441 日志判断

INMP441 适配启用时，日志会同时报告 `slot0`、`slot1` 和最终 `mono(slot0)`：

```text
[audio_test] INMP441 adapter: slot=0 shift=16 output=mono int16; reporting slot0/slot1/mono
[audio_test] slot0 min=-6486 max=28395 mean=3985 rms=8993 zero=64/16000 clip=0 nearclip=0
[audio_test] slot1 min=0 max=0 mean=0 rms=0 zero=16000/16000 clip=0 nearclip=0
```

判断规则：

- `slot0` 有数据、`slot1` 全 0：符合 `L/R` 接 GND 的 INMP441。
- 两路都全 0：检查 VDD/GND/BCLK/WS/SD 接线和 `/dev/audio/pcm_in1`。
- 数据在另一 slot：检查 `L/R` 接法或把 slot 改为 1。
- `clip` 或 `nearclip` 持续增加：输入过大或右移太小，优先增大 shift。
- 敲击或咳嗽时 `rms` 明显升高：采集链路基本可用，再回到 `audio_event` 验证模型。
