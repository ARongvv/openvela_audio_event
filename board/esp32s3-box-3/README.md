# ESP32-S3-BOX-3 Vendor Board

This custom board is based on the upstream NuttX
`boards/xtensa/esp32s3/esp32s3-box` implementation. It adds the BOX-3
ES7210 microphone initialization and registers I2S1 RX as
`/dev/audio/pcm_in1` for the `audio_event` demo.

The board is exposed by the team manifest at:

```text
vendor/espressif/boards/esp32s3/esp32s3-box-3
```

Build it from the openvela root:

```bash
source build/envsetup.sh
./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/audio_event/ \
  -j8
```

The ES7210 register sequence is an initial hardware bring-up configuration.
Validate channel layout, sample rate, gain, and clipping with recorded PCM
before model calibration.
