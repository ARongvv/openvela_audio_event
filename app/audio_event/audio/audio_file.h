/*
 * HostFS/raw file audio source used by simulator tests.
 */

#ifndef __APPS_EXAMPLES_AUDIO_EVENT_AUDIO_AUDIO_FILE_H
#define __APPS_EXAMPLES_AUDIO_EVENT_AUDIO_AUDIO_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct audio_file_s
{
  int fd;
  off_t data_offset;
  size_t data_size;
  size_t data_read;
  unsigned int repeats_left;
};

int audio_file_open(struct audio_file_s *source, const char *path,
                    unsigned int repeat_count);
ssize_t audio_file_read(struct audio_file_s *source, int16_t *samples,
                        size_t sample_count);
void audio_file_close(struct audio_file_s *source);

#endif /* __APPS_EXAMPLES_AUDIO_EVENT_AUDIO_AUDIO_FILE_H */
