/*
 * TensorFlow-compatible log-mel + delta feature extraction.
 */

#ifndef __APPS_EXAMPLES_AUDIO_EVENT_DSP_FEATURE_EXTRACT_H
#define __APPS_EXAMPLES_AUDIO_EVENT_DSP_FEATURE_EXTRACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

int feature_extract_init(void);
void feature_extract_deinit(void);

int feature_extract_compute(const int16_t *samples, size_t sample_count,
                            float *features, size_t feature_count);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_EXAMPLES_AUDIO_EVENT_DSP_FEATURE_EXTRACT_H */
