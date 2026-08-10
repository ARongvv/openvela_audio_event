/* Optional, lossy UDP PCM streamer for the audio-event dashboard. */

#ifndef __APPS_EXAMPLES_AUDIO_EVENT_STREAMER_PCM_STREAMER_H
#define __APPS_EXAMPLES_AUDIO_EVENT_STREAMER_PCM_STREAMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PCM_STREAM_FRAME_SAMPLES 512

/* This storage is owned by the audio application's PSRAM workspace.  Keeping
 * the frame representation here lets the owner reserve an exact-sized slice
 * without requiring a second, small heap allocation. */

struct pcm_stream_frame_s
{
  uint32_t sequence;
  uint64_t timestamp_ms;
  uint16_t sample_count;
  int16_t samples[PCM_STREAM_FRAME_SAMPLES];
};

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM
#  define PCM_STREAM_QUEUE_STORAGE_SIZE \
  (CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_QUEUE_DEPTH * \
   sizeof(struct pcm_stream_frame_s))
#else
#  define PCM_STREAM_QUEUE_STORAGE_SIZE 0
#endif

struct pcm_streamer_stats_s
{
  uint32_t queued_frames;
  uint32_t sent_frames;
  uint32_t queue_dropped_frames;
  uint32_t send_dropped_frames;
  int last_error;
  bool enabled;
};

int pcm_streamer_init(bool enabled, void *queue_storage,
                      size_t queue_storage_size);
void pcm_streamer_deinit(void);
int pcm_streamer_submit(const int16_t *samples, size_t sample_count,
                        uint64_t timestamp_ms);
void pcm_streamer_get_stats(struct pcm_streamer_stats_s *stats);

#endif
