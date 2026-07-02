/*
 * NuttX Audio capture source.
 */

#ifndef __APPS_EXAMPLES_AUDIO_EVENT_AUDIO_AUDIO_CAPTURE_H
#define __APPS_EXAMPLES_AUDIO_EVENT_AUDIO_AUDIO_CAPTURE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

int audio_capture_init(const char *device_path);
ssize_t audio_capture_read(int16_t *samples, size_t sample_count);
void audio_capture_deinit(void);

#endif /* __APPS_EXAMPLES_AUDIO_EVENT_AUDIO_AUDIO_CAPTURE_H */
