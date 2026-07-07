# ESP32-S3 DevKit audio_event board

This board-local directory is based on the upstream NuttX
`esp32s3-devkit` board and adds the configuration needed by the
`ccf_audioevent` project.  The intended hardware is an ESP32-S3-N16R8
development board connected to an external INMP441 digital microphone.

## Vendor symlink

Create the vendor board entry as a symbolic link to this directory:

```bash
cd /home/arongw/openvela
mkdir -p vendor/espressif/boards/esp32s3
ln -sfn /home/arongw/openvela/ccf_audioevent/board/esp32s3-devkit \
  vendor/espressif/boards/esp32s3/esp32s3-devkit
```

The `audio_event` defconfig uses `CONFIG_ARCH_BOARD_CUSTOM`, so the build
loads the board files from the vendor symlink instead of the NuttX in-tree
`esp32s3-devkit` board.

## INMP441 wiring

The current `audio_event` config uses I2S1 RX:

| INMP441 signal | ESP32-S3 GPIO | Config |
| --- | --- | --- |
| SCK / BCLK | GPIO18 | `CONFIG_ESP32S3_I2S1_BCLKPIN=18` |
| WS / LRCK | GPIO17 | `CONFIG_ESP32S3_I2S1_WSPIN=17` |
| SD / DOUT | GPIO15 | `CONFIG_ESP32S3_I2S1_DINPIN=15` |

INMP441 has no I2C control path.  Board bring-up only needs to register
the generic ESP32-S3 I2S RX audio device.

## Build

```bash
cd /home/arongw/openvela
./build.sh vendor/espressif/boards/esp32s3/esp32s3-devkit/configs/audio_event/ -j8
```

## Flash and monitor

```bash
cd /home/arongw/openvela
cd nuttx
make flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BAUD=921600
cd ..
picocom -b 115200 /dev/ttyACM0
```

## Smoke tests

After NSH starts:

```text
audio_test --device /dev/audio/pcm_in1 --channels 2 --seconds 5
audio_event --model-smoke
audio_event --device /dev/audio/pcm_in1 --audio-stats
```

The board path now exposes the I2S capture device.  If `audio_test`
shows non-zero samples but `audio_event` classification is still poor,
the next app-level step is to adapt `audio_event` capture from INMP441
32-bit I2S samples to the model's 16-bit mono PCM input.
