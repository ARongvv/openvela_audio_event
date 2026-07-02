/****************************************************************************
 * board/esp32s3-box-3/src/esp32s3_es7210.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARD_ESP32S3_BOX_3_SRC_ESP32S3_ES7210_H
#define __BOARD_ESP32S3_BOX_3_SRC_ESP32S3_ES7210_H

#include <nuttx/config.h>

#include <stdint.h>

#ifdef CONFIG_ESP32S3_BOX_AUDIO
int esp32s3_es7210_initialize(int i2c_port, uint8_t i2c_addr,
                              uint32_t i2c_frequency);
#endif

#endif /* __BOARD_ESP32S3_BOX_3_SRC_ESP32S3_ES7210_H */
