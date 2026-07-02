/*
 * Blocking PCM reader built on the asynchronous NuttX Audio API.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>

#include "audio/audio_capture.h"
#include "audio_event_config.h"

#define AUDIO_CAPTURE_MAX_BUFFERS 8
#define AUDIO_CAPTURE_FALLBACK_MAX_BUFFERS 4
#define AUDIO_CAPTURE_FALLBACK_MAX_BYTES 2048

static int g_audio_fd = -1;
static mqd_t g_audio_mq = (mqd_t)-1;
static char g_audio_mqname[32];
static struct ap_buffer_s *g_audio_buffers[AUDIO_CAPTURE_MAX_BUFFERS];
static unsigned int g_audio_buffer_count;
static struct ap_buffer_s *g_current_buffer;
static size_t g_current_offset;
static bool g_audio_started;
static bool g_audio_use_local_buffers;

#ifdef CONFIG_AUDIO_MULTI_SESSION
static void *g_audio_session;
#endif

static int enqueue_buffer(struct ap_buffer_s *buffer)
{
  struct audio_buf_desc_s descriptor;

  buffer->curbyte = 0;
  buffer->nbytes = buffer->nmaxbytes;
  buffer->flags = 0;
  memset(&descriptor, 0, sizeof(descriptor));
#ifdef CONFIG_AUDIO_MULTI_SESSION
  descriptor.session = g_audio_session;
#endif
  descriptor.numbytes = buffer->nbytes;
  descriptor.u.buffer = buffer;

  if (ioctl(g_audio_fd, AUDIOIOC_ENQUEUEBUFFER,
            (unsigned long)(uintptr_t)&descriptor) < 0)
    {
      return -errno;
    }

  return 0;
}

static int wait_for_buffer(void)
{
  struct audio_msg_s message;
  unsigned int priority;

  for (;;)
    {
      ssize_t size = mq_receive(g_audio_mq, (char *)&message,
                                sizeof(message), &priority);
      if (size < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return -errno;
        }

      if (size != sizeof(message))
        {
          continue;
        }

      if (message.msg_id == AUDIO_MSG_DEQUEUE)
        {
          g_current_buffer = message.u.ptr;
          g_current_offset = 0;
          return 0;
        }

      if (message.msg_id == AUDIO_MSG_COMPLETE ||
          message.msg_id == AUDIO_MSG_STOP)
        {
          return -EPIPE;
        }
    }
}

int audio_capture_init(const char *device_path)
{
  struct audio_caps_desc_s capabilities;
  struct ap_buffer_info_s buffer_info;
  struct mq_attr attributes;
  unsigned int i;
  int ret;

  if (device_path == NULL || g_audio_fd >= 0)
    {
      return -EINVAL;
    }

  printf("[audio] open %s\n", device_path);
  g_audio_fd = open(device_path, O_RDWR | O_CLOEXEC);
  if (g_audio_fd < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio] cannot open %s: %d\n", device_path, ret);
      return ret;
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  ret = ioctl(g_audio_fd, AUDIOIOC_RESERVE,
              (unsigned long)(uintptr_t)&g_audio_session);
#else
  ret = ioctl(g_audio_fd, AUDIOIOC_RESERVE, 0);
#endif
  if (ret < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio] reserve failed: %d\n", ret);
      goto fail;
    }

  memset(&capabilities, 0, sizeof(capabilities));
#ifdef CONFIG_AUDIO_MULTI_SESSION
  capabilities.session = g_audio_session;
#endif
  capabilities.caps.ac_len = sizeof(struct audio_caps_s);
  capabilities.caps.ac_type = AUDIO_TYPE_INPUT;
  capabilities.caps.ac_subtype = AUDIO_FMT_PCM;
  capabilities.caps.ac_channels = 1;
  capabilities.caps.ac_controls.hw[0] = AUDIO_EVENT_SAMPLE_RATE;
  capabilities.caps.ac_controls.b[3] = AUDIO_EVENT_SAMPLE_RATE >> 16;
  capabilities.caps.ac_controls.b[2] = 16;

  printf("[audio] configure input pcm rate=%d channels=1 bits=16\n",
         AUDIO_EVENT_SAMPLE_RATE);
  if (ioctl(g_audio_fd, AUDIOIOC_CONFIGURE,
            (unsigned long)(uintptr_t)&capabilities) < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio] configure failed: %d\n", ret);
      goto fail;
    }

  memset(&buffer_info, 0, sizeof(buffer_info));
  if (ioctl(g_audio_fd, AUDIOIOC_GETBUFFERINFO,
            (unsigned long)(uintptr_t)&buffer_info) < 0)
    {
      ret = -errno;
      printf("[audio] get buffer info failed: %d, fallback to local buffers\n",
             ret);
      buffer_info.nbuffers = CONFIG_AUDIO_NUM_BUFFERS;
      buffer_info.buffer_size = CONFIG_AUDIO_BUFFER_NUMBYTES;
      if (buffer_info.nbuffers > AUDIO_CAPTURE_FALLBACK_MAX_BUFFERS)
        {
          buffer_info.nbuffers = AUDIO_CAPTURE_FALLBACK_MAX_BUFFERS;
        }

      if (buffer_info.buffer_size > AUDIO_CAPTURE_FALLBACK_MAX_BYTES)
        {
          buffer_info.buffer_size = AUDIO_CAPTURE_FALLBACK_MAX_BYTES;
        }

      g_audio_use_local_buffers = true;
    }

  printf("[audio] buffer info: nbuffers=%u size=%u bytes\n",
         buffer_info.nbuffers, buffer_info.buffer_size);
  if (buffer_info.nbuffers == 0 ||
      buffer_info.nbuffers > AUDIO_CAPTURE_MAX_BUFFERS)
    {
      ret = -E2BIG;
      fprintf(stderr, "[audio] unsupported buffer count: %u max=%u\n",
              buffer_info.nbuffers, AUDIO_CAPTURE_MAX_BUFFERS);
      goto fail;
    }

  snprintf(g_audio_mqname, sizeof(g_audio_mqname), "/audio_event_%ld",
           (long)getpid());
  memset(&attributes, 0, sizeof(attributes));
  attributes.mq_maxmsg = buffer_info.nbuffers + 4;
  attributes.mq_msgsize = sizeof(struct audio_msg_s);
  printf("[audio] mq open %s maxmsg=%ld msgsize=%ld\n",
         g_audio_mqname, attributes.mq_maxmsg, attributes.mq_msgsize);
  g_audio_mq = mq_open(g_audio_mqname, O_RDWR | O_CREAT, 0644, &attributes);
  if (g_audio_mq == (mqd_t)-1)
    {
      ret = -errno;
      fprintf(stderr, "[audio] mq_open failed: %d\n", ret);
      goto fail;
    }

  if (ioctl(g_audio_fd, AUDIOIOC_REGISTERMQ,
            (unsigned long)g_audio_mq) < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio] register mq failed: %d\n", ret);
      goto fail;
    }
  printf("[audio] mq registered\n");

  for (i = 0; i < buffer_info.nbuffers; i++)
    {
      struct audio_buf_desc_s descriptor;
      int ioctl_ret;

      memset(&descriptor, 0, sizeof(descriptor));
#ifdef CONFIG_AUDIO_MULTI_SESSION
      descriptor.session = g_audio_session;
#endif
      descriptor.numbytes = buffer_info.buffer_size;
      descriptor.u.pbuffer = &g_audio_buffers[i];
      printf("[audio] alloc buffer %u/%u size=%u\n",
             i + 1, buffer_info.nbuffers, buffer_info.buffer_size);

      if (g_audio_use_local_buffers)
        {
          ioctl_ret = apb_alloc(&descriptor);
        }
      else
        {
          ioctl_ret = ioctl(g_audio_fd, AUDIOIOC_ALLOCBUFFER,
                            (unsigned long)(uintptr_t)&descriptor);
        }

      if (ioctl_ret != sizeof(descriptor))
        {
          ret = ioctl_ret < 0 ? ioctl_ret : -ENOMEM;
          fprintf(stderr,
                  "[audio] alloc buffer %u failed: ioctl_ret=%d status=%d\n",
                  i + 1, ioctl_ret, ret);
          goto fail;
        }

      g_audio_buffer_count++;
      ret = enqueue_buffer(g_audio_buffers[i]);
      if (ret < 0)
        {
          fprintf(stderr, "[audio] enqueue buffer %u failed: %d\n",
                  i + 1, ret);
          goto fail;
        }
      printf("[audio] buffer %u enqueued\n", i + 1);
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  ret = ioctl(g_audio_fd, AUDIOIOC_START,
              (unsigned long)(uintptr_t)g_audio_session);
#else
  ret = ioctl(g_audio_fd, AUDIOIOC_START, 0);
#endif
  if (ret < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio] start failed: %d\n", ret);
      goto fail;
    }

  g_audio_started = true;
  printf("[audio] capturing %s at %d Hz, mono, 16-bit\n", device_path,
         AUDIO_EVENT_SAMPLE_RATE);
  return 0;

fail:
  audio_capture_deinit();
  return ret;
}

ssize_t audio_capture_read(int16_t *samples, size_t sample_count)
{
  size_t copied = 0;

  if (g_audio_fd < 0 || !g_audio_started || samples == NULL ||
      sample_count == 0)
    {
      return -EINVAL;
    }

  while (copied < sample_count)
    {
      size_t available;
      size_t take;

      if (g_current_buffer == NULL ||
          g_current_offset >= g_current_buffer->nbytes)
        {
          int ret;
          if (g_current_buffer != NULL)
            {
              ret = enqueue_buffer(g_current_buffer);
              g_current_buffer = NULL;
              if (ret < 0)
                {
                  return copied > 0 ? (ssize_t)copied : ret;
                }
            }

          ret = wait_for_buffer();
          if (ret < 0)
            {
              return copied > 0 ? (ssize_t)copied : ret;
            }
        }

      available = (g_current_buffer->nbytes - g_current_offset) /
                  sizeof(int16_t);
      take = sample_count - copied;
      if (take > available)
        {
          take = available;
        }

      memcpy(samples + copied,
             g_current_buffer->samp + g_current_offset,
             take * sizeof(int16_t));
      copied += take;
      g_current_offset += take * sizeof(int16_t);
    }

  return copied;
}

void audio_capture_deinit(void)
{
  unsigned int i;

  if (g_audio_fd >= 0 && g_audio_started)
    {
#ifdef CONFIG_AUDIO_MULTI_SESSION
      ioctl(g_audio_fd, AUDIOIOC_STOP,
            (unsigned long)(uintptr_t)g_audio_session);
#else
      ioctl(g_audio_fd, AUDIOIOC_STOP, 0);
#endif
    }

  g_audio_started = false;
  g_current_buffer = NULL;
  g_current_offset = 0;

  if (g_audio_fd >= 0 && g_audio_mq != (mqd_t)-1)
    {
      ioctl(g_audio_fd, AUDIOIOC_UNREGISTERMQ,
            (unsigned long)g_audio_mq);
    }

  for (i = 0; i < g_audio_buffer_count; i++)
    {
      struct audio_buf_desc_s descriptor;
      if (g_audio_buffers[i] == NULL)
        {
          continue;
        }

      if (g_audio_use_local_buffers)
        {
          apb_free(g_audio_buffers[i]);
          g_audio_buffers[i] = NULL;
          continue;
        }

      memset(&descriptor, 0, sizeof(descriptor));
#ifdef CONFIG_AUDIO_MULTI_SESSION
      descriptor.session = g_audio_session;
#endif
      descriptor.u.buffer = g_audio_buffers[i];
      ioctl(g_audio_fd, AUDIOIOC_FREEBUFFER,
            (unsigned long)(uintptr_t)&descriptor);
      g_audio_buffers[i] = NULL;
    }

  g_audio_buffer_count = 0;
  g_audio_use_local_buffers = false;
  if (g_audio_fd >= 0)
    {
#ifdef CONFIG_AUDIO_MULTI_SESSION
      ioctl(g_audio_fd, AUDIOIOC_RELEASE,
            (unsigned long)(uintptr_t)g_audio_session);
#else
      ioctl(g_audio_fd, AUDIOIOC_RELEASE, 0);
#endif
      close(g_audio_fd);
      g_audio_fd = -1;
    }

  if (g_audio_mq != (mqd_t)-1)
    {
      mq_close(g_audio_mq);
      mq_unlink(g_audio_mqname);
      g_audio_mq = (mqd_t)-1;
    }
}
