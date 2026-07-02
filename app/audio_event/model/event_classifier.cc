/*
 * TFLite Micro classifier for the audio event model.
 */

#include <nuttx/config.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "audio_event_config.h"
#include "model/event_classifier.h"
#include "model/model.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace
{

alignas(16) uint8_t g_tensor_arena[CONFIG_EXAMPLES_AUDIO_EVENT_ARENA_SIZE];

tflite::MicroInterpreter *g_interpreter;
TfLiteTensor *g_input;
TfLiteTensor *g_output;
size_t g_arena_used;

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
      class_count != AUDIO_EVENT_CLASS_COUNT)
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

  const tflite::Model *model = tflite::GetModel(g_audio_event_model);
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
          resolver.AddMean() != kTfLiteOk ||
          resolver.AddFullyConnected() != kTfLiteOk ||
          resolver.AddSoftmax() != kTfLiteOk)
        {
          std::fprintf(stderr, "[model] failed to register operators\n");
          return -EINVAL;
        }

      resolver_initialized = true;
    }

  static tflite::MicroInterpreter interpreter(
      model, resolver, g_tensor_arena, sizeof(g_tensor_arena));

  if (interpreter.AllocateTensors() != kTfLiteOk)
    {
      std::fprintf(stderr, "[model] AllocateTensors failed, arena=%zu\n",
                   sizeof(g_tensor_arena));
      return -ENOMEM;
    }

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
      tensor_element_count(g_output) != AUDIO_EVENT_CLASS_COUNT)
    {
      std::fprintf(stderr,
                   "[model] expected int8 output with %d elements, "
                   "got type=%d\n",
                   AUDIO_EVENT_CLASS_COUNT,
                   g_output == nullptr ? -1 :
                                         static_cast<int>(g_output->type));
      event_classifier_deinit();
      return -EINVAL;
    }

  std::printf("[model] arena=%lu used=%lu model=%u bytes\n",
              static_cast<unsigned long>(sizeof(g_tensor_arena)),
              static_cast<unsigned long>(g_arena_used),
              g_audio_event_model_len);
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
    const int8_t *features, size_t feature_count, float *probabilities,
    size_t class_count)
{
  if (g_interpreter == nullptr || g_input == nullptr || features == nullptr ||
      feature_count != AUDIO_EVENT_FEATURE_SIZE ||
      class_count != AUDIO_EVENT_CLASS_COUNT)
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

extern "C" int event_classifier_predict(const float *features,
                                        size_t feature_count,
                                        float *probabilities,
                                        size_t class_count)
{
  if (g_interpreter == nullptr || g_input == nullptr || features == nullptr ||
      feature_count != AUDIO_EVENT_FEATURE_SIZE ||
      class_count != AUDIO_EVENT_CLASS_COUNT ||
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

extern "C" size_t event_classifier_arena_used(void)
{
  return g_arena_used;
}
