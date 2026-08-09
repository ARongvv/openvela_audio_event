/*
 * TFLite Micro classifier for the audio event model.
 */

#include <nuttx/config.h>

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_ARENA_PSRAM
#include <nuttx/kmalloc.h>
#endif

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "audio_event_config.h"
#include "model/event_classifier.h"
#if defined(CONFIG_EXAMPLES_AUDIO_EVENT_MODEL_8CLASS) || \
    defined(CONFIG_EXAMPLES_TFLM_BENCHMARK_S3_LARGE_8CLASS)
#include "model/s3_large_8class_model.h"
#else
#include "model/model.h"
#endif

#ifdef CONFIG_TFLITEMICRO_ESP32S3_CCOUNT_PROFILER
#include <arch/xtensa/core_macros.h>
#endif

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#ifdef CONFIG_TFLITEMICRO_DEBUG
#include "tensorflow/lite/micro/micro_profiler.h"
#endif
#include "tensorflow/lite/schema/schema_generated.h"

#ifdef CONFIG_TFLITEMICRO_ESP_NN_MEAN
namespace tflite::esp_nn
{
TFLMRegistration Register_MEAN();
}
#endif

namespace
{

#if defined(CONFIG_EXAMPLES_AUDIO_EVENT_MODEL_8CLASS) || \
    defined(CONFIG_EXAMPLES_TFLM_BENCHMARK_S3_LARGE_8CLASS)
constexpr size_t kClassCount = AUDIO_EVENT_CLASS_COUNT;
constexpr const char *kModelTag = "s3-large-8class";
#define AUDIO_EVENT_ACTIVE_MODEL g_s3_model
#define AUDIO_EVENT_ACTIVE_MODEL_LEN g_s3_model_len
#else
constexpr size_t kClassCount = AUDIO_EVENT_CLASS_COUNT;
constexpr const char *kModelTag = "model";
#define AUDIO_EVENT_ACTIVE_MODEL g_audio_event_model
#define AUDIO_EVENT_ACTIVE_MODEL_LEN g_audio_event_model_len
#endif

#ifndef EVENT_CLASSIFIER_ARENA_SIZE
#define EVENT_CLASSIFIER_ARENA_SIZE CONFIG_EXAMPLES_AUDIO_EVENT_ARENA_SIZE
#endif

/* The normal 8-class profile keeps its ~193 KiB tensor arena as a static
 * internal-DRAM array for the fastest ESP-NN path. The remote HTTP profile
 * can instead allocate it from PSRAM, leaving internal DRAM for Wi-Fi.
 */

static uint8_t *g_tensor_arena;

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_MODEL_8CLASS
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_ARENA_PSRAM
constexpr unsigned int kArenaPsramAllocAttempts = 3;

extern "C" uint32_t esp_spiram_allocable_vaddr_start(void);
extern "C" uint32_t esp_spiram_allocable_vaddr_end(void);

static bool classifier_arena_in_psram(const void *arena, size_t size)
{
  uintptr_t start = (uintptr_t)esp_spiram_allocable_vaddr_start();
  uintptr_t end = (uintptr_t)esp_spiram_allocable_vaddr_end();
  uintptr_t address = (uintptr_t)arena;

  return address >= start && address <= end && size <= end - address;
}

static uint8_t *classifier_arena_alloc(size_t size)
{
  void *internal_guards[kArenaPsramAllocAttempts - 1] = {nullptr};
  unsigned int guard_count = 0;
  uint8_t *arena = nullptr;

  /* kmm_memalign searches both common-heap regions. Keep any internal-DRAM
   * candidate temporarily allocated, then retry until the large arena lands
   * in PSRAM. This mirrors the audio workspace allocation policy.
   */

  for (unsigned int attempt = 0; attempt < kArenaPsramAllocAttempts;
       attempt++)
    {
      arena = static_cast<uint8_t *>(kmm_memalign(16, size));
      if (arena == nullptr)
        {
          break;
        }

      if (classifier_arena_in_psram(arena, size))
        {
          break;
        }

      if (guard_count == kArenaPsramAllocAttempts - 1)
        {
          kmm_free(arena);
          arena = nullptr;
          break;
        }

      internal_guards[guard_count++] = arena;
      arena = nullptr;
    }

  for (unsigned int attempt = 0; attempt < guard_count; attempt++)
    {
      kmm_free(internal_guards[attempt]);
    }

  if (arena == nullptr || !classifier_arena_in_psram(arena, size))
    {
      std::fprintf(stderr,
                   "[model] PSRAM arena allocation failed: %u bytes\n",
                   (unsigned int)size);
      return nullptr;
    }

  std::printf("[model] arena PSRAM ptr=0x%08lx size=%u align_16=%d\n",
              (unsigned long)(uintptr_t)arena, (unsigned int)size,
              ((uintptr_t)arena & 0x0f) == 0);
  return arena;
}

static void classifier_arena_free(uint8_t *arena)
{
  kmm_free(arena);
}
#else
static uint8_t *classifier_arena_alloc(size_t size)
{
  /* 8-class performance profiles keep this static DRAM arena. */

  alignas(16) static uint8_t s_static_arena[EVENT_CLASSIFIER_ARENA_SIZE];

  (void)size;

  std::printf("[model] arena static DRAM size=%u\n",
              (unsigned int)EVENT_CLASSIFIER_ARENA_SIZE);
  return s_static_arena;
}

static void classifier_arena_free(uint8_t *arena)
{
  (void)arena;
}
#endif
#else
static uint8_t *classifier_arena_alloc(size_t size)
{
  alignas(16) static uint8_t s_static_arena[EVENT_CLASSIFIER_ARENA_SIZE];

  (void)size;

  return s_static_arena;
}

static void classifier_arena_free(uint8_t *arena)
{
  (void)arena;
}
#endif

tflite::MicroInterpreter *g_interpreter;
TfLiteTensor *g_input;
TfLiteTensor *g_output;
size_t g_arena_used;

#ifdef CONFIG_TFLITEMICRO_DEBUG
tflite::MicroProfiler g_profiler;
#endif

int tensor_element_count(const TfLiteTensor *tensor)
{
  int count = 1;

  if (tensor == nullptr || tensor->dims == nullptr)
    {
      return 0;
    }

  for (int i = 0; i < tensor->dims->size; i++)
    {
      count *= tensor->dims->data[i];
    }

  return count;
}

int read_output(float *probabilities, size_t class_count)
{
  if (g_output == nullptr || probabilities == nullptr ||
      class_count != kClassCount)
    {
      return -EINVAL;
    }

  for (size_t i = 0; i < class_count; i++)
    {
      probabilities[i] =
          (static_cast<int>(g_output->data.int8[i]) -
           g_output->params.zero_point) *
          g_output->params.scale;
    }

  return 0;
}

}  // namespace

extern "C" int event_classifier_init(void)
{
  static tflite::MicroMutableOpResolver<9> resolver;
  static bool resolver_initialized;

  if (g_tensor_arena == nullptr)
    {
      g_tensor_arena = classifier_arena_alloc(EVENT_CLASSIFIER_ARENA_SIZE);
      if (g_tensor_arena == nullptr)
        {
          std::fprintf(stderr, "[model] arena alloc failed: %u bytes\n",
                       (unsigned int)EVENT_CLASSIFIER_ARENA_SIZE);
          return -ENOMEM;
        }

      std::printf("[model] arena ptr=0x%08lx size=%u align_256=%d\n",
                  (unsigned long)(uintptr_t)g_tensor_arena,
                  (unsigned int)EVENT_CLASSIFIER_ARENA_SIZE,
                  ((uintptr_t)g_tensor_arena & 0xFF) == 0);
    }

  const tflite::Model *model = tflite::GetModel(AUDIO_EVENT_ACTIVE_MODEL);
  if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION)
    {
      std::fprintf(stderr, "[model] schema mismatch: model=%d runtime=%d\n",
                   model == nullptr ? -1 : model->version(),
                   TFLITE_SCHEMA_VERSION);
      return -EINVAL;
    }

  if (!resolver_initialized)
    {
      if (resolver.AddShape() != kTfLiteOk ||
          resolver.AddStridedSlice() != kTfLiteOk ||
          resolver.AddPack() != kTfLiteOk ||
          resolver.AddReshape() != kTfLiteOk ||
          resolver.AddConv2D() != kTfLiteOk ||
          resolver.AddDepthwiseConv2D() != kTfLiteOk ||
#ifdef CONFIG_TFLITEMICRO_ESP_NN_MEAN
          resolver.AddMean(tflite::esp_nn::Register_MEAN()) != kTfLiteOk ||
#else
          resolver.AddMean() != kTfLiteOk ||
#endif
          resolver.AddFullyConnected() != kTfLiteOk ||
          resolver.AddSoftmax() != kTfLiteOk)
        {
          std::fprintf(stderr, "[model] failed to register operators\n");
          return -EINVAL;
        }

      resolver_initialized = true;
    }

#ifdef CONFIG_TFLITEMICRO_DEBUG
  static tflite::MicroInterpreter interpreter(
      model, resolver, g_tensor_arena, EVENT_CLASSIFIER_ARENA_SIZE, nullptr,
      &g_profiler);
#else
  static tflite::MicroInterpreter interpreter(
      model, resolver, g_tensor_arena, EVENT_CLASSIFIER_ARENA_SIZE);
#endif
  static bool tensors_allocated;

  if (!tensors_allocated && interpreter.AllocateTensors() != kTfLiteOk)
    {
      std::fprintf(stderr, "[model] AllocateTensors failed, arena=%zu\n",
                   (size_t)EVENT_CLASSIFIER_ARENA_SIZE);
      return -ENOMEM;
    }

  tensors_allocated = true;

  g_interpreter = &interpreter;
  g_input = interpreter.input(0);
  g_output = interpreter.output(0);
  g_arena_used = interpreter.arena_used_bytes();

  if (g_input == nullptr || g_input->type != kTfLiteInt8 ||
      tensor_element_count(g_input) != AUDIO_EVENT_FEATURE_SIZE)
    {
      std::fprintf(stderr,
                   "[model] expected int8 input with %d elements, "
                   "got type=%d\n",
                   AUDIO_EVENT_FEATURE_SIZE,
                   g_input == nullptr ? -1 : static_cast<int>(g_input->type));
      event_classifier_deinit();
      return -EINVAL;
    }

  if (g_output == nullptr || g_output->type != kTfLiteInt8 ||
      tensor_element_count(g_output) != kClassCount)
    {
      std::fprintf(stderr,
                   "[model] expected int8 output with %d elements, "
                   "got type=%d\n",
                   static_cast<int>(kClassCount),
                   g_output == nullptr ? -1 :
                                         static_cast<int>(g_output->type));
      event_classifier_deinit();
      return -EINVAL;
    }

  std::printf("[%s] arena=%lu used=%lu model=%u bytes\n", kModelTag,
              static_cast<unsigned long>(EVENT_CLASSIFIER_ARENA_SIZE),
              static_cast<unsigned long>(g_arena_used),
              AUDIO_EVENT_ACTIVE_MODEL_LEN);
  return 0;
}

extern "C" void event_classifier_deinit(void)
{
  g_interpreter = nullptr;
  g_input = nullptr;
  g_output = nullptr;
  g_arena_used = 0;

  if (g_tensor_arena != nullptr)
    {
      classifier_arena_free(g_tensor_arena);
      g_tensor_arena = nullptr;
    }
}

extern "C" int event_classifier_predict_quantized(
    const int8_t *features, size_t feature_count, float *probabilities,
    size_t class_count)
{
  if (g_interpreter == nullptr || g_input == nullptr || features == nullptr ||
      feature_count != AUDIO_EVENT_FEATURE_SIZE ||
      class_count != kClassCount)
    {
      return -EINVAL;
    }

  std::memcpy(g_input->data.int8, features, feature_count);
  if (g_interpreter->Invoke() != kTfLiteOk)
    {
      std::fprintf(stderr, "[model] Invoke failed\n");
      return -EIO;
    }

  return read_output(probabilities, class_count);
}

extern "C" int event_classifier_benchmark_invoke_quantized(
    const int8_t *features, size_t feature_count, uint32_t *invoke_cycles,
    int8_t *output, size_t output_count)
{
#ifndef CONFIG_TFLITEMICRO_ESP32S3_CCOUNT_PROFILER
  (void)features;
  (void)feature_count;
  (void)invoke_cycles;
  (void)output;
  (void)output_count;
  return -ENOTSUP;
#else
  TfLiteStatus invoke_status;
  uint32_t start_cycles;
  uint32_t end_cycles;

  if (g_interpreter == nullptr || g_input == nullptr || g_output == nullptr ||
      features == nullptr || invoke_cycles == nullptr || output == nullptr ||
      feature_count != AUDIO_EVENT_FEATURE_SIZE ||
      output_count != kClassCount)
    {
      return -EINVAL;
    }

  std::memcpy(g_input->data.int8, features, feature_count);
#ifdef CONFIG_TFLITEMICRO_DEBUG
  g_profiler.ClearEvents();
#endif
  start_cycles = static_cast<uint32_t>(XTHAL_GET_CCOUNT());
  invoke_status = g_interpreter->Invoke();
  end_cycles = static_cast<uint32_t>(XTHAL_GET_CCOUNT());
#ifdef CONFIG_TFLITEMICRO_DEBUG
  g_profiler.ClearEvents();
#endif
  if (invoke_status != kTfLiteOk)
    {
      std::fprintf(stderr, "[model] Invoke failed\n");
      return -EIO;
    }

  *invoke_cycles = end_cycles - start_cycles;
  std::memcpy(output, g_output->data.int8, output_count);
  return 0;
#endif
}

extern "C" uint32_t event_classifier_benchmark_ticks_per_second(void)
{
#ifdef CONFIG_TFLITEMICRO_ESP32S3_CCOUNT_PROFILER
  return static_cast<uint32_t>(CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ) *
         1000000u;
#else
  return 0;
#endif
}

extern "C" int event_classifier_predict(const float *features,
                                        size_t feature_count,
                                        float *probabilities,
                                        size_t class_count)
{
  if (g_interpreter == nullptr || g_input == nullptr || features == nullptr ||
      feature_count != AUDIO_EVENT_FEATURE_SIZE ||
      class_count != kClassCount ||
      g_input->params.scale <= 0.0f)
    {
      return -EINVAL;
    }

  for (size_t i = 0; i < feature_count; i++)
    {
      long value = std::lround(features[i] / g_input->params.scale) +
                   g_input->params.zero_point;
      if (value < -128)
        {
          value = -128;
        }
      else if (value > 127)
        {
          value = 127;
        }

      g_input->data.int8[i] = static_cast<int8_t>(value);
    }

  if (g_interpreter->Invoke() != kTfLiteOk)
    {
      std::fprintf(stderr, "[model] Invoke failed\n");
      return -EIO;
    }

  return read_output(probabilities, class_count);
}

extern "C" int event_classifier_profile(const float *features,
                                         size_t feature_count,
                                         unsigned int warmup_count,
                                         unsigned int repeat_count, bool csv)
{
#ifndef CONFIG_TFLITEMICRO_DEBUG
  (void)features;
  (void)feature_count;
  (void)warmup_count;
  (void)repeat_count;
  (void)csv;
  std::fprintf(stderr,
               "[tflm_benchmark] unavailable: enable "
               "CONFIG_TFLITEMICRO_DEBUG\n");
  return -ENOTSUP;
#else
  float probabilities[kClassCount];
  unsigned int run;
  int ret;

  if (features == nullptr || feature_count != AUDIO_EVENT_FEATURE_SIZE ||
      repeat_count == 0)
    {
      return -EINVAL;
    }

  g_profiler.ClearEvents();
  for (run = 0; run < warmup_count; run++)
    {
      ret = event_classifier_predict(features, feature_count, probabilities,
                                     kClassCount);
      if (ret < 0)
        {
          return ret;
        }

      g_profiler.ClearEvents();
    }

  std::printf("[tflm_benchmark] warmup=%u repeat=%u csv=%u\n", warmup_count,
              repeat_count, csv ? 1 : 0);
  for (run = 0; run < repeat_count; run++)
    {
      ret = event_classifier_predict(features, feature_count, probabilities,
                                     kClassCount);
      if (ret < 0)
        {
          return ret;
        }

      std::printf("[tflm_benchmark] iteration=%u\n", run + 1);
      if (csv)
        {
          g_profiler.LogCsv();
        }
      else
        {
          g_profiler.Log();
        }

      g_profiler.ClearEvents();
    }

  return 0;
#endif
}

extern "C" size_t event_classifier_arena_used(void)
{
  return g_arena_used;
}
