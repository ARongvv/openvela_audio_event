/*
 * TFLite Micro inference wrapper for audio event classification.
 */

#ifndef __APPS_EXAMPLES_AUDIO_EVENT_MODEL_EVENT_CLASSIFIER_H
#define __APPS_EXAMPLES_AUDIO_EVENT_MODEL_EVENT_CLASSIFIER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

int event_classifier_init(void);
void event_classifier_deinit(void);

int event_classifier_predict(const float *features, size_t feature_count,
                             float *probabilities, size_t class_count);

int event_classifier_predict_quantized(const int8_t *features,
                                       size_t feature_count,
                                       float *probabilities,
                                       size_t class_count);

size_t event_classifier_arena_used(void);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_EXAMPLES_AUDIO_EVENT_MODEL_EVENT_CLASSIFIER_H */
