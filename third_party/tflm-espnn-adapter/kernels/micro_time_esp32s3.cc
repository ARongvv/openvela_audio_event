/*
 * ESP32-S3 CCOUNT time source for TFLite Micro profiling.
 */

#include <nuttx/config.h>

#include <arch/xtensa/core_macros.h>

#include "tensorflow/lite/micro/micro_time.h"

namespace tflite {

uint32_t ticks_per_second()
{
  return static_cast<uint32_t>(CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ) *
         1000000u;
}

uint32_t GetCurrentTimeTicks()
{
  return static_cast<uint32_t>(XTHAL_GET_CCOUNT());
}

}  // namespace tflite
