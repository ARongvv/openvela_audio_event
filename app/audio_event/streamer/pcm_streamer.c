/* Bounded, lossy UDP PCM streamer. Network I/O never runs in audio capture. */

#include <nuttx/config.h>

#include "streamer/pcm_streamer.h"

#include <errno.h>
#include <string.h>

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "audio_event_config.h"

#define PCM_STREAM_MAGIC 0x41455043u /* "AEPC" */
#define PCM_STREAM_VERSION 1

struct __attribute__((packed)) pcm_packet_header_s
{
  uint32_t magic;
  uint16_t version;
  uint32_t sequence;
  uint64_t timestamp_ms;
  uint16_t sample_count;
  uint16_t flags;
};

struct pcm_streamer_s
{
  pthread_mutex_t lock;
  sem_t wake_sem;
  pthread_t thread;
  struct sockaddr_in destination;
  struct pcm_stream_frame_s *frames;
  struct pcm_streamer_stats_s stats;
  unsigned int head;
  unsigned int count;
  uint32_t next_sequence;
  int socket_fd;
  bool initialized;
  bool stopping;
  bool thread_created;
};

static struct pcm_streamer_s g_streamer;

static uint64_t pcm_htonll(uint64_t value)
{
  return ((uint64_t)htonl((uint32_t)(value >> 32))) |
         ((uint64_t)htonl((uint32_t)value) << 32);
}

static void pcm_note_send_drop(int error)
{
  pthread_mutex_lock(&g_streamer.lock);
  g_streamer.stats.send_dropped_frames++;
  g_streamer.stats.last_error = error;
  pthread_mutex_unlock(&g_streamer.lock);
}

static void *pcm_streamer_thread(void *argument)
{
  (void)argument;

  for (;;)
    {
      struct pcm_stream_frame_s frame;
      uint8_t packet[sizeof(struct pcm_packet_header_s) +
                     PCM_STREAM_FRAME_SAMPLES * sizeof(int16_t)];
      struct pcm_packet_header_s *header =
          (struct pcm_packet_header_s *)packet;
      ssize_t sent;
      size_t packet_size;

      while (sem_wait(&g_streamer.wake_sem) < 0)
        {
          if (errno != EINTR)
            {
              return NULL;
            }
        }

      pthread_mutex_lock(&g_streamer.lock);
      if (g_streamer.stopping)
        {
          pthread_mutex_unlock(&g_streamer.lock);
          return NULL;
        }

      if (g_streamer.count == 0)
        {
          pthread_mutex_unlock(&g_streamer.lock);
          continue;
        }

      frame = g_streamer.frames[g_streamer.head];
      g_streamer.head = (g_streamer.head + 1) %
                        CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_QUEUE_DEPTH;
      g_streamer.count--;
      g_streamer.stats.queued_frames = g_streamer.count;
      pthread_mutex_unlock(&g_streamer.lock);

      header->magic = htonl(PCM_STREAM_MAGIC);
      header->version = htons(PCM_STREAM_VERSION);
      header->sequence = htonl(frame.sequence);
      header->timestamp_ms = pcm_htonll(frame.timestamp_ms);
      header->sample_count = htons(frame.sample_count);
      header->flags = 0;
      memcpy(packet + sizeof(*header), frame.samples,
             frame.sample_count * sizeof(int16_t));
      packet_size = sizeof(*header) + frame.sample_count * sizeof(int16_t);
      sent = sendto(g_streamer.socket_fd, packet, packet_size, 0,
                    (const struct sockaddr *)&g_streamer.destination,
                    sizeof(g_streamer.destination));
      if (sent != (ssize_t)packet_size)
        {
          pcm_note_send_drop(sent < 0 ? -errno : -EIO);
        }
      else
        {
          pthread_mutex_lock(&g_streamer.lock);
          g_streamer.stats.sent_frames++;
          pthread_mutex_unlock(&g_streamer.lock);
        }
    }
}

int pcm_streamer_init(bool enabled, void *queue_storage,
                      size_t queue_storage_size)
{
  pthread_attr_t attr;
  struct sched_param param;
  int ret;
  int flags;
  bool attr_initialized = false;

  if (!enabled)
    {
      return 0;
    }

  if (CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_HOST[0] == '\0')
    {
      return -EINVAL;
    }

  memset(&g_streamer, 0, sizeof(g_streamer));
  g_streamer.socket_fd = -1;
  if (queue_storage == NULL ||
      queue_storage_size < PCM_STREAM_QUEUE_STORAGE_SIZE)
    {
      return -ENOMEM;
    }

  g_streamer.frames = queue_storage;

  ret = pthread_mutex_init(&g_streamer.lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  if (sem_init(&g_streamer.wake_sem, 0, 0) < 0)
    {
      pthread_mutex_destroy(&g_streamer.lock);
      return -errno;
    }

  g_streamer.socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_streamer.socket_fd < 0)
    {
      ret = -errno;
      goto error;
    }

  flags = fcntl(g_streamer.socket_fd, F_GETFL, 0);
  if (flags < 0 || fcntl(g_streamer.socket_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
      ret = -errno;
      goto error;
    }

  g_streamer.destination.sin_family = AF_INET;
  g_streamer.destination.sin_port =
      htons(CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_PORT);
  if (inet_pton(AF_INET, CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_HOST,
                &g_streamer.destination.sin_addr) != 1)
    {
      ret = -EINVAL;
      goto error;
    }

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      ret = -ret;
      goto error;
    }

  attr_initialized = true;

  ret = pthread_attr_setstacksize(
      &attr, CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_STACKSIZE);
  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedpolicy(&attr, SCHED_RR);
    }

  if (ret == 0)
    {
      memset(&param, 0, sizeof(param));
      param.sched_priority = CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_PRIORITY;
      ret = pthread_attr_setschedparam(&attr, &param);
    }

  if (ret == 0)
    {
      ret = pthread_create(&g_streamer.thread, &attr, pcm_streamer_thread,
                           NULL);
    }

  pthread_attr_destroy(&attr);
  attr_initialized = false;
  if (ret != 0)
    {
      ret = -ret;
      goto error;
    }

  g_streamer.thread_created = true;
  g_streamer.initialized = true;
  g_streamer.stats.enabled = true;
  printf("[pcm] udp enabled host=%s port=%d queue=%d storage=%p psram=1\n",
         CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_HOST,
         CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_PORT,
         CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_QUEUE_DEPTH,
         queue_storage);
  return 0;

error:
  if (attr_initialized)
    {
      pthread_attr_destroy(&attr);
    }

  if (g_streamer.socket_fd >= 0)
    {
      close(g_streamer.socket_fd);
    }

  sem_destroy(&g_streamer.wake_sem);
  pthread_mutex_destroy(&g_streamer.lock);
  memset(&g_streamer, 0, sizeof(g_streamer));
  return ret;
}

void pcm_streamer_deinit(void)
{
  if (!g_streamer.initialized)
    {
      return;
    }

  pthread_mutex_lock(&g_streamer.lock);
  g_streamer.stopping = true;
  pthread_mutex_unlock(&g_streamer.lock);
  sem_post(&g_streamer.wake_sem);
  pthread_join(g_streamer.thread, NULL);
  close(g_streamer.socket_fd);
  sem_destroy(&g_streamer.wake_sem);
  pthread_mutex_destroy(&g_streamer.lock);
  memset(&g_streamer, 0, sizeof(g_streamer));
}

int pcm_streamer_submit(const int16_t *samples, size_t sample_count,
                        uint64_t timestamp_ms)
{
  struct pcm_stream_frame_s *frame;
  unsigned int tail;

  if (!g_streamer.initialized)
    {
      return 0;
    }

  if (samples == NULL || sample_count == 0 ||
      sample_count > PCM_STREAM_FRAME_SAMPLES)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_streamer.lock);
  if (g_streamer.count == CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_QUEUE_DEPTH)
    {
      g_streamer.stats.queue_dropped_frames++;
      pthread_mutex_unlock(&g_streamer.lock);
      return -ENOSPC;
    }

  tail = (g_streamer.head + g_streamer.count) %
         CONFIG_EXAMPLES_AUDIO_EVENT_PCM_STREAM_QUEUE_DEPTH;
  frame = &g_streamer.frames[tail];
  frame->sequence = ++g_streamer.next_sequence;
  frame->timestamp_ms = timestamp_ms;
  frame->sample_count = sample_count;
  memcpy(frame->samples, samples, sample_count * sizeof(int16_t));
  g_streamer.count++;
  g_streamer.stats.queued_frames = g_streamer.count;
  pthread_mutex_unlock(&g_streamer.lock);
  sem_post(&g_streamer.wake_sem);
  return 0;
}

void pcm_streamer_get_stats(struct pcm_streamer_stats_s *stats)
{
  if (stats == NULL)
    {
      return;
    }

  if (!g_streamer.initialized)
    {
      memset(stats, 0, sizeof(*stats));
      return;
    }

  pthread_mutex_lock(&g_streamer.lock);
  *stats = g_streamer.stats;
  pthread_mutex_unlock(&g_streamer.lock);
}

#else
int pcm_streamer_init(bool enabled, void *queue_storage,
                      size_t queue_storage_size)
{
  (void)queue_storage;
  (void)queue_storage_size;
  return enabled ? -ENOSYS : 0;
}

void pcm_streamer_deinit(void)
{
}

int pcm_streamer_submit(const int16_t *samples, size_t count, uint64_t time)
{
  (void)samples;
  (void)count;
  (void)time;
  return 0;
}

void pcm_streamer_get_stats(struct pcm_streamer_stats_s *stats)
{
  if (stats != NULL)
    {
      memset(stats, 0, sizeof(*stats));
    }
}
#endif
