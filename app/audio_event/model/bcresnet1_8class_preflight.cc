// Isolated TFLM preflight wrapper for the BC-ResNet1 8-class model.

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "audio_event_config.h"
#include "model/bcresnet1_8class_model.h"
#include "model/event_classifier.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_profiler.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace
{

constexpr size_t kFeatureCount = 49 * 40 * 3;
constexpr size_t kClassCount = 8;

#ifndef EVENT_CLASSIFIER_ARENA_SIZE
#define EVENT_CLASSIFIER_ARENA_SIZE CONFIG_EXAMPLES_TFLM_BENCHMARK_ARENA_SIZE
#endif

alignas(16) uint8_t g_tensor_arena[EVENT_CLASSIFIER_ARENA_SIZE];
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

int read_output(float *values, size_t class_count)
{
  if (g_output == nullptr || values == nullptr || class_count != kClassCount)
    {
      return -EINVAL;
    }

  for (size_t i = 0; i < class_count; i++)
    {
      values[i] = (static_cast<int>(g_output->data.int8[i]) -
                   g_output->params.zero_point) * g_output->params.scale;
    }

  return 0;
}

}  // namespace

extern "C" int event_classifier_init(void)
{
  static tflite::MicroMutableOpResolver<14> resolver;
  static bool resolver_initialized;
  const tflite::Model *model = tflite::GetModel(g_bcresnet1_8class_model);

  if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION)
    {
      std::fprintf(stderr,
                   "[bcresnet-preflight] schema mismatch: model=%d runtime=%d\n",
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
          resolver.AddMul() != kTfLiteOk || resolver.AddAdd() != kTfLiteOk ||
          resolver.AddConcatenation() != kTfLiteOk ||
          resolver.AddMean() != kTfLiteOk ||
          resolver.AddLogistic() != kTfLiteOk ||
          resolver.AddSpaceToBatchNd() != kTfLiteOk ||
          resolver.AddBatchToSpaceNd() != kTfLiteOk ||
          resolver.AddFullyConnected() != kTfLiteOk)
        {
          std::fprintf(stderr,
                       "[bcresnet-preflight] failed to register operators\n");
          return -EINVAL;
        }

      resolver_initialized = true;
    }

#ifdef CONFIG_TFLITEMICRO_DEBUG
  static tflite::MicroInterpreter interpreter(
      model, resolver, g_tensor_arena, sizeof(g_tensor_arena), nullptr,
      &g_profiler);
#else
  static tflite::MicroInterpreter interpreter(
      model, resolver, g_tensor_arena, sizeof(g_tensor_arena));
#endif

  if (interpreter.AllocateTensors() != kTfLiteOk)
    {
      std::fprintf(stderr,
                   "[bcresnet-preflight] AllocateTensors failed, arena=%zu\n",
                   sizeof(g_tensor_arena));
      return -ENOMEM;
    }

  g_interpreter = &interpreter;
  g_input = interpreter.input(0);
  g_output = interpreter.output(0);
  g_arena_used = interpreter.arena_used_bytes();

  if (g_input == nullptr || g_input->type != kTfLiteInt8 ||
      tensor_element_count(g_input) != kFeatureCount ||
      g_output == nullptr || g_output->type != kTfLiteInt8 ||
      tensor_element_count(g_output) != kClassCount)
    {
      std::fprintf(stderr,
                   "[bcresnet-preflight] expected int8 input=%u output=%u, "
                   "got input_type=%d input_count=%d output_type=%d "
                   "output_count=%d\n",
                   static_cast<unsigned int>(kFeatureCount),
                   static_cast<unsigned int>(kClassCount),
                   g_input == nullptr ? -1 : static_cast<int>(g_input->type),
                   tensor_element_count(g_input),
                   g_output == nullptr ? -1 : static_cast<int>(g_output->type),
                   tensor_element_count(g_output));
      event_classifier_deinit();
      return -EINVAL;
    }

  std::printf("[bcresnet-preflight] arena=%lu used=%lu model=%u bytes "
              "input_scale=%g input_zp=%d output_scale=%g output_zp=%d\n",
              static_cast<unsigned long>(sizeof(g_tensor_arena)),
              static_cast<unsigned long>(g_arena_used),
              g_bcresnet1_8class_model_len, g_input->params.scale,
              g_input->params.zero_point, g_output->params.scale,
              g_output->params.zero_point);
  return 0;
}

extern "C" void event_classifier_deinit(void)
{
  g_interpreter = nullptr;
  g_input = nullptr;
  g_output = nullptr;
  g_arena_used = 0;
}

extern "C" int event_classifier_predict_quantized(
    const int8_t *features, size_t feature_count, float *values,
    size_t class_count)
{
  if (g_interpreter == nullptr || g_input == nullptr || features == nullptr ||
      feature_count != kFeatureCount || class_count != kClassCount)
    {
      return -EINVAL;
    }

  std::memcpy(g_input->data.int8, features, feature_count);
  if (g_interpreter->Invoke() != kTfLiteOk)
    {
      std::fprintf(stderr, "[bcresnet-preflight] Invoke failed\n");
      return -EIO;
    }

  return read_output(values, class_count);
}

extern "C" int event_classifier_predict(const float *features,
                                          size_t feature_count,
                                          float *values,
                                          size_t class_count)
{
  if (g_interpreter == nullptr || g_input == nullptr || features == nullptr ||
      feature_count != kFeatureCount || class_count != kClassCount ||
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
      std::fprintf(stderr, "[bcresnet-preflight] Invoke failed\n");
      return -EIO;
    }

  return read_output(values, class_count);
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
               "[bcresnet-preflight] unavailable: enable "
               "CONFIG_TFLITEMICRO_DEBUG\n");
  return -ENOTSUP;
#else
  float values[kClassCount];
  unsigned int run;
  int ret;

  if (features == nullptr || feature_count != kFeatureCount ||
      repeat_count == 0)
    {
      return -EINVAL;
    }

  g_profiler.ClearEvents();
  for (run = 0; run < warmup_count; run++)
    {
      ret = event_classifier_predict(features, feature_count, values,
                                     kClassCount);
      if (ret < 0)
        {
          return ret;
        }

      g_profiler.ClearEvents();
    }

  std::printf("[bcresnet-preflight] warmup=%u repeat=%u csv=%u\n",
              warmup_count, repeat_count, csv ? 1 : 0);
  for (run = 0; run < repeat_count; run++)
    {
      ret = event_classifier_predict(features, feature_count, values,
                                     kClassCount);
      if (ret < 0)
        {
          return ret;
        }

      std::printf("[bcresnet-preflight] iteration=%u output=", run + 1);
      for (size_t i = 0; i < kClassCount; i++)
        {
          std::printf("%s%.5f", i == 0 ? "" : ",", values[i]);
        }

      std::printf("\n");
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
