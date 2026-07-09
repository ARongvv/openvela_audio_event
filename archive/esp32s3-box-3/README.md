# ESP32-S3-BOX-3 Archive

This directory keeps the retired ESP32-S3-BOX-3 adaptation for reference.
It is not an active `ccf_audioevent` target.

The active hardware path is:

```text
ESP32-S3 DevKit + INMP441 + 0.96 OLED
```

Archived contents:

- `board/`: historical custom board source for ESP32-S3-BOX-3.
- `scripts/`: temporary BOX-3 build patch scripts and LittleFS helper.
- `docs/`: schematic notes, hardware initialization notes, ES7210 audio
  investigation notes, and old BOX-3 test flow.

Reason for archival:

- BOX-3 ES7210 register access worked, but the real capture path did not
  produce a stable non-zero PCM stream.
- DevKit + INMP441 reached a usable microphone, recording, OLED, and
  `audio_event` validation loop.
- Keeping BOX-3 in the main path made the README, manifest, and build flow
  harder to follow.

Use this directory only when revisiting the BOX-3 ES7210/LCD/touch bring-up
history. New development should use `board/esp32s3-devkit`.
