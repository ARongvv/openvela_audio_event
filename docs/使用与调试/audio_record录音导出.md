# audio_record 录音导出工具

`audio_record` 用来把真机采集到的 INMP441 音频导出为 WAV，方便在电脑上听、看波形、
检查削波和静音段。它不是主检测应用。

## 常用命令

短录音：

```text
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 2
```

最长录音由 `CONFIG_EXAMPLES_AUDIO_RECORD_MAX_SECONDS` 控制，当前为 60 秒：

```text
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 60
```

也可以临时指定 INMP441 slot 和缩放：

```text
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 3 --slot 0 --shift 16
```

## 导出 WAV

`audio_record` 不会自动把文件保存到电脑。它会先在板端内存中缓存录音，采集完成后在
串口输出 WAV 的 base64：

```text
WAV_BASE64_BEGIN
...
WAV_BASE64_END
```

电脑端只复制两行 marker 中间的 base64 正文到 `record.b64`，然后解码：

```bash
base64 -d record.b64 > record.wav
```

长录音建议用 `picocom --logfile` 保存串口日志：

```bash
picocom -b 115200 /dev/ttyACM0 --logfile audio_record.log
```

NSH 中执行：

```text
nsh> audio_record --device /dev/audio/pcm_in1 --seconds 60
```

退出 `picocom` 后提取 WAV：

```bash
sed -n '/WAV_BASE64_BEGIN/,/WAV_BASE64_END/p' audio_record.log \
  | sed '1d;$d' \
  | tr -d '\r' \
  | base64 -d > record.wav
```

检查和播放：

```bash
file record.wav
ls -lh record.wav
aplay record.wav
```

正常应是 16 kHz、mono、16-bit PCM WAV。60 秒缓存约占：

```text
16000 samples/s * 60 s * 2 bytes = 1,920,000 bytes
```

因此长录音依赖 ESP32-S3-N16R8 的 PSRAM 已加入 heap。如果提示分配失败，先确认固件是
用 `esp32s3-devkit/configs/audio_event` 重新 configure 并烧录的。

注意：`record.b64` 中只能保留 base64 正文，不要混入 `WAV_BASE64_BEGIN`、
`WAV_BASE64_END`、`nsh>` 或 `[audio_record]` 日志。
