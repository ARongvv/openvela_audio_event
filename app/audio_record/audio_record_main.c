/*
 * Short audio recorder that exports a WAV file as base64 text.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>

#ifndef CONFIG_EXAMPLES_AUDIO_RECORD_DEVPATH
#  define CONFIG_EXAMPLES_AUDIO_RECORD_DEVPATH "/dev/audio/pcm_in1"
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_RECORD_SAMPLE_RATE
#  define CONFIG_EXAMPLES_AUDIO_RECORD_SAMPLE_RATE 16000
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_RECORD_SECONDS
#  define CONFIG_EXAMPLES_AUDIO_RECORD_SECONDS 2
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_RECORD_MAX_SECONDS
#  define CONFIG_EXAMPLES_AUDIO_RECORD_MAX_SECONDS 60
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_RECORD_BLOCK_FRAMES
#  define CONFIG_EXAMPLES_AUDIO_RECORD_BLOCK_FRAMES 512
#endif

#ifndef CONFIG_AUDIO_NUM_BUFFERS
#  define CONFIG_AUDIO_NUM_BUFFERS 4
#endif

#ifndef CONFIG_AUDIO_BUFFER_NUMBYTES
#  define CONFIG_AUDIO_BUFFER_NUMBYTES 2048
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_SLOT
#  define CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_SLOT 0
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_SHIFT
#  define CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_SHIFT 16
#endif

#define AUDIO_RECORD_OUTPUT_CHANNELS 1
#define AUDIO_RECORD_OUTPUT_BITS 16
#define AUDIO_RECORD_MAX_BUFFERS 8
#define AUDIO_RECORD_FALLBACK_MAX_BUFFERS 4
#define AUDIO_RECORD_FALLBACK_MAX_BYTES 2048
#define AUDIO_RECORD_MAX_BLOCK_FRAMES 1024
#define AUDIO_RECORD_WAV_HEADER_SIZE 44
#define AUDIO_RECORD_B64_COLUMNS 76
#define AUDIO_RECORD_STOP_DRAIN_MS 100

#ifdef CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_32BIT
#  define AUDIO_RECORD_DEVICE_CHANNELS 2
#  define AUDIO_RECORD_DEVICE_BITS 32
#  define AUDIO_RECORD_SLOT_BYTES 4
#  define AUDIO_RECORD_FRAME_BYTES \
    (AUDIO_RECORD_DEVICE_CHANNELS * AUDIO_RECORD_SLOT_BYTES)
#else
#  define AUDIO_RECORD_DEVICE_CHANNELS 1
#  define AUDIO_RECORD_DEVICE_BITS 16
#endif

struct audio_record_options_s
{
  const char *device;
  unsigned int rate;
  unsigned int seconds;
  unsigned int block_frames;
  unsigned int slot;
  unsigned int shift;
};

struct audio_record_capture_s
{
  int fd;
  mqd_t mq;
  char mqname[32];
  struct ap_buffer_s *buffers[AUDIO_RECORD_MAX_BUFFERS];
  unsigned int buffer_count;
  struct ap_buffer_s *current;
  size_t current_offset;
  bool started;
  bool use_local_buffers;
#ifdef CONFIG_AUDIO_MULTI_SESSION
  void *session;
#endif
};

struct audio_record_stats_s
{
  int16_t min;
  int16_t max;
  int64_t sum;
  uint64_t sumsq;
  unsigned int zero_count;
  unsigned int clip_count;
  unsigned int samples;
  bool initialized;
};

struct audio_record_b64_s
{
  uint8_t pending[3];
  unsigned int pending_len;
  unsigned int column;
};

static int16_t g_read_buffer[AUDIO_RECORD_MAX_BLOCK_FRAMES];

static void audio_record_usage(void)
{
  printf("Usage: audio_record [options]\n");
  printf("Options:\n");
  printf("  --device PATH        audio input device, default %s\n",
         CONFIG_EXAMPLES_AUDIO_RECORD_DEVPATH);
  printf("  --rate HZ           sample rate, default %d\n",
         CONFIG_EXAMPLES_AUDIO_RECORD_SAMPLE_RATE);
  printf("  --seconds N         duration 1..%d, default %d\n",
         CONFIG_EXAMPLES_AUDIO_RECORD_MAX_SECONDS,
         CONFIG_EXAMPLES_AUDIO_RECORD_SECONDS);
  printf("  --block-frames N    frames per read 1..%d, default %d\n",
         AUDIO_RECORD_MAX_BLOCK_FRAMES,
         CONFIG_EXAMPLES_AUDIO_RECORD_BLOCK_FRAMES);
#ifdef CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_32BIT
  printf("  --slot N            INMP441 slot 0 or 1, default %d\n",
         CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_SLOT);
  printf("  --shift N           int32 to int16 right shift 0..24, "
         "default %d\n", CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_SHIFT);
#endif
  printf("  --help              show this message\n");
}

static int parse_uint_arg(const char *text, unsigned int *value)
{
  char *endptr;
  unsigned long parsed;

  if (text == NULL || value == NULL || text[0] == '\0')
    {
      return -EINVAL;
    }

  errno = 0;
  parsed = strtoul(text, &endptr, 10);
  if (errno != 0 || *endptr != '\0' || parsed > UINT_MAX)
    {
      return -EINVAL;
    }

  *value = (unsigned int)parsed;
  return 0;
}

static int parse_options(int argc, char *argv[],
                         struct audio_record_options_s *options)
{
  int i;

  options->device = CONFIG_EXAMPLES_AUDIO_RECORD_DEVPATH;
  options->rate = CONFIG_EXAMPLES_AUDIO_RECORD_SAMPLE_RATE;
  options->seconds = CONFIG_EXAMPLES_AUDIO_RECORD_SECONDS;
  options->block_frames = CONFIG_EXAMPLES_AUDIO_RECORD_BLOCK_FRAMES;
  options->slot = CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_SLOT;
  options->shift = CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_SHIFT;

  for (i = 1; i < argc; i++)
    {
      const char *arg = argv[i];
      unsigned int value;

      if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
        {
          audio_record_usage();
          return 1;
        }
      else if (strcmp(arg, "--device") == 0)
        {
          if (++i >= argc)
            {
              return -EINVAL;
            }

          options->device = argv[i];
        }
      else if (strcmp(arg, "--rate") == 0)
        {
          if (++i >= argc || parse_uint_arg(argv[i], &value) < 0)
            {
              return -EINVAL;
            }

          options->rate = value;
        }
      else if (strcmp(arg, "--seconds") == 0)
        {
          if (++i >= argc || parse_uint_arg(argv[i], &value) < 0)
            {
              return -EINVAL;
            }

          options->seconds = value;
        }
      else if (strcmp(arg, "--block-frames") == 0)
        {
          if (++i >= argc || parse_uint_arg(argv[i], &value) < 0)
            {
              return -EINVAL;
            }

          options->block_frames = value;
        }
      else if (strcmp(arg, "--slot") == 0)
        {
          if (++i >= argc || parse_uint_arg(argv[i], &value) < 0)
            {
              return -EINVAL;
            }

          options->slot = value;
        }
      else if (strcmp(arg, "--shift") == 0)
        {
          if (++i >= argc || parse_uint_arg(argv[i], &value) < 0)
            {
              return -EINVAL;
            }

          options->shift = value;
        }
      else
        {
          fprintf(stderr, "[audio_record] unknown option: %s\n", arg);
          return -EINVAL;
        }
    }

  if (options->device == NULL || options->device[0] == '\0' ||
      options->rate < 8000 || options->rate > 48000 ||
      options->seconds == 0 ||
      options->seconds > CONFIG_EXAMPLES_AUDIO_RECORD_MAX_SECONDS ||
      options->block_frames == 0 ||
      options->block_frames > AUDIO_RECORD_MAX_BLOCK_FRAMES ||
      options->slot > 1 || options->shift > 24)
    {
      return -EINVAL;
    }

  return 0;
}

static uint32_t isqrt_u64(uint64_t value)
{
  uint64_t root = 0;
  uint64_t bit = (uint64_t)1 << 62;

  while (bit > value)
    {
      bit >>= 2;
    }

  while (bit != 0)
    {
      if (value >= root + bit)
        {
          value -= root + bit;
          root = (root >> 1) + bit;
        }
      else
        {
          root >>= 1;
        }

      bit >>= 2;
    }

  return (uint32_t)root;
}

static void stats_update(struct audio_record_stats_s *stats,
                         const int16_t *samples,
                         unsigned int count)
{
  unsigned int i;

  for (i = 0; i < count; i++)
    {
      int16_t sample = samples[i];
      int32_t widened = sample;

      if (!stats->initialized)
        {
          stats->min = sample;
          stats->max = sample;
          stats->initialized = true;
        }
      else
        {
          if (sample < stats->min)
            {
              stats->min = sample;
            }

          if (sample > stats->max)
            {
              stats->max = sample;
            }
        }

      stats->sum += sample;
      stats->sumsq += (uint64_t)(widened * widened);
      stats->samples++;

      if (sample == 0)
        {
          stats->zero_count++;
        }

      if (sample == INT16_MIN || sample == INT16_MAX)
        {
          stats->clip_count++;
        }
    }
}

static void stats_print(const struct audio_record_stats_s *stats)
{
  int64_t mean = 0;
  uint32_t rms = 0;

  if (stats->samples > 0)
    {
      mean = stats->sum / stats->samples;
      rms = isqrt_u64(stats->sumsq / stats->samples);
    }

  printf("[audio_record] stats samples=%u min=%d max=%d mean=%lld rms=%lu "
         "zero=%u/%u clip=%u\n",
         stats->samples, stats->initialized ? stats->min : 0,
         stats->initialized ? stats->max : 0,
         (long long)mean, (unsigned long)rms,
         stats->zero_count, stats->samples, stats->clip_count);
}

#ifdef CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_32BIT
static int16_t clamp_int16(int32_t value)
{
  if (value > INT16_MAX)
    {
      return INT16_MAX;
    }

  if (value < INT16_MIN)
    {
      return INT16_MIN;
    }

  return (int16_t)value;
}

static int32_t load_le32(const uint8_t *data)
{
  uint32_t value = (uint32_t)data[0] |
                   ((uint32_t)data[1] << 8) |
                   ((uint32_t)data[2] << 16) |
                   ((uint32_t)data[3] << 24);

  return (int32_t)value;
}

static int16_t convert_inmp441_sample(const uint8_t *frame,
                                      unsigned int slot_index,
                                      unsigned int shift)
{
  const uint8_t *slot = frame + slot_index * AUDIO_RECORD_SLOT_BYTES;
  int32_t raw = load_le32(slot);
  int32_t shifted = raw >> shift;

  return clamp_int16(shifted);
}
#endif

static int enqueue_buffer(struct audio_record_capture_s *capture,
                          struct ap_buffer_s *buffer)
{
  struct audio_buf_desc_s descriptor;

  buffer->curbyte = 0;
  buffer->nbytes = buffer->nmaxbytes;
  buffer->flags = 0;
  memset(&descriptor, 0, sizeof(descriptor));
#ifdef CONFIG_AUDIO_MULTI_SESSION
  descriptor.session = capture->session;
#endif
  descriptor.numbytes = buffer->nbytes;
  descriptor.u.buffer = buffer;

  if (ioctl(capture->fd, AUDIOIOC_ENQUEUEBUFFER,
            (unsigned long)(uintptr_t)&descriptor) < 0)
    {
      return -errno;
    }

  return 0;
}

static int wait_for_buffer(struct audio_record_capture_s *capture)
{
  struct audio_msg_s message;
  unsigned int priority;

  for (;;)
    {
      ssize_t size = mq_receive(capture->mq, (char *)&message,
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
          capture->current = message.u.ptr;
          capture->current_offset = 0;
          return 0;
        }

      if (message.msg_id == AUDIO_MSG_COMPLETE ||
          message.msg_id == AUDIO_MSG_STOP)
        {
          return -EPIPE;
        }
    }
}

static void drain_messages(struct audio_record_capture_s *capture)
{
  struct audio_msg_s message;
  struct mq_attr old_attr;
  struct mq_attr new_attr;
  unsigned int priority;
  bool restore_attr = false;

  if (capture->mq == (mqd_t)-1)
    {
      return;
    }

  if (mq_getattr(capture->mq, &old_attr) == 0)
    {
      new_attr = old_attr;
      new_attr.mq_flags |= O_NONBLOCK;
      if (mq_setattr(capture->mq, &new_attr, NULL) == 0)
        {
          restore_attr = true;
        }
    }

  for (;;)
    {
      ssize_t size = mq_receive(capture->mq, (char *)&message,
                                sizeof(message), &priority);
      if (size < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          break;
        }
    }

  if (restore_attr)
    {
      mq_setattr(capture->mq, &old_attr, NULL);
    }
}

static void capture_deinit(struct audio_record_capture_s *capture)
{
  unsigned int i;

  if (capture->fd >= 0 && capture->started)
    {
#ifdef CONFIG_AUDIO_MULTI_SESSION
      ioctl(capture->fd, AUDIOIOC_STOP,
            (unsigned long)(uintptr_t)capture->session);
#else
      ioctl(capture->fd, AUDIOIOC_STOP, 0);
#endif
      usleep(AUDIO_RECORD_STOP_DRAIN_MS * 1000);
      drain_messages(capture);
    }

  capture->started = false;
  capture->current = NULL;
  capture->current_offset = 0;

  if (capture->fd >= 0 && capture->mq != (mqd_t)-1)
    {
      ioctl(capture->fd, AUDIOIOC_UNREGISTERMQ,
            (unsigned long)capture->mq);
    }

  for (i = 0; i < capture->buffer_count; i++)
    {
      struct audio_buf_desc_s descriptor;

      if (capture->buffers[i] == NULL)
        {
          continue;
        }

      if (capture->use_local_buffers)
        {
          apb_free(capture->buffers[i]);
          capture->buffers[i] = NULL;
          continue;
        }

      memset(&descriptor, 0, sizeof(descriptor));
#ifdef CONFIG_AUDIO_MULTI_SESSION
      descriptor.session = capture->session;
#endif
      descriptor.u.buffer = capture->buffers[i];
      ioctl(capture->fd, AUDIOIOC_FREEBUFFER,
            (unsigned long)(uintptr_t)&descriptor);
      capture->buffers[i] = NULL;
    }

  capture->buffer_count = 0;
  capture->use_local_buffers = false;

  if (capture->fd >= 0)
    {
#ifdef CONFIG_AUDIO_MULTI_SESSION
      ioctl(capture->fd, AUDIOIOC_RELEASE,
            (unsigned long)(uintptr_t)capture->session);
#else
      ioctl(capture->fd, AUDIOIOC_RELEASE, 0);
#endif
      close(capture->fd);
      capture->fd = -1;
    }

  if (capture->mq != (mqd_t)-1)
    {
      mq_close(capture->mq);
      mq_unlink(capture->mqname);
      capture->mq = (mqd_t)-1;
    }
}

static int capture_init(struct audio_record_capture_s *capture,
                        const struct audio_record_options_s *options)
{
  struct audio_caps_desc_s capabilities;
  struct ap_buffer_info_s buffer_info;
  struct mq_attr attributes;
  unsigned int i;
  int ret;

  memset(capture, 0, sizeof(*capture));
  capture->fd = -1;
  capture->mq = (mqd_t)-1;

  printf("[audio_record] open %s\n", options->device);
  capture->fd = open(options->device, O_RDWR | O_CLOEXEC);
  if (capture->fd < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio_record] cannot open %s: %d\n",
              options->device, ret);
      return ret;
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  ret = ioctl(capture->fd, AUDIOIOC_RESERVE,
              (unsigned long)(uintptr_t)&capture->session);
#else
  ret = ioctl(capture->fd, AUDIOIOC_RESERVE, 0);
#endif
  if (ret < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio_record] reserve failed: %d\n", ret);
      goto fail;
    }

  memset(&capabilities, 0, sizeof(capabilities));
#ifdef CONFIG_AUDIO_MULTI_SESSION
  capabilities.session = capture->session;
#endif
  capabilities.caps.ac_len = sizeof(struct audio_caps_s);
  capabilities.caps.ac_type = AUDIO_TYPE_INPUT;
  capabilities.caps.ac_subtype = AUDIO_FMT_PCM;
  capabilities.caps.ac_channels = AUDIO_RECORD_DEVICE_CHANNELS;
  capabilities.caps.ac_controls.hw[0] = options->rate & 0xffff;
  capabilities.caps.ac_controls.b[3] = options->rate >> 16;
  capabilities.caps.ac_controls.b[2] = AUDIO_RECORD_DEVICE_BITS;

  printf("[audio_record] configure input pcm rate=%u channels=%u bits=%u\n",
         options->rate, AUDIO_RECORD_DEVICE_CHANNELS,
         AUDIO_RECORD_DEVICE_BITS);
#ifdef CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_32BIT
  printf("[audio_record] INMP441 adapter: slot=%u shift=%u "
         "output=mono int16\n", options->slot, options->shift);
#endif

  if (ioctl(capture->fd, AUDIOIOC_CONFIGURE,
            (unsigned long)(uintptr_t)&capabilities) < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio_record] configure failed: %d\n", ret);
      goto fail;
    }

  memset(&buffer_info, 0, sizeof(buffer_info));
  if (ioctl(capture->fd, AUDIOIOC_GETBUFFERINFO,
            (unsigned long)(uintptr_t)&buffer_info) < 0)
    {
      ret = -errno;
      printf("[audio_record] get buffer info failed: %d, "
             "fallback to local buffers\n", ret);
      buffer_info.nbuffers = CONFIG_AUDIO_NUM_BUFFERS;
      buffer_info.buffer_size = CONFIG_AUDIO_BUFFER_NUMBYTES;

      if (buffer_info.nbuffers > AUDIO_RECORD_FALLBACK_MAX_BUFFERS)
        {
          buffer_info.nbuffers = AUDIO_RECORD_FALLBACK_MAX_BUFFERS;
        }

      if (buffer_info.buffer_size > AUDIO_RECORD_FALLBACK_MAX_BYTES)
        {
          buffer_info.buffer_size = AUDIO_RECORD_FALLBACK_MAX_BYTES;
        }

      capture->use_local_buffers = true;
    }

  printf("[audio_record] buffer info: nbuffers=%u size=%u bytes\n",
         buffer_info.nbuffers, buffer_info.buffer_size);
  if (buffer_info.nbuffers == 0 ||
      buffer_info.nbuffers > AUDIO_RECORD_MAX_BUFFERS)
    {
      ret = -E2BIG;
      fprintf(stderr, "[audio_record] unsupported buffer count: %u max=%u\n",
              buffer_info.nbuffers, AUDIO_RECORD_MAX_BUFFERS);
      goto fail;
    }

  snprintf(capture->mqname, sizeof(capture->mqname), "/audio_record_%ld",
           (long)getpid());
  memset(&attributes, 0, sizeof(attributes));
  attributes.mq_maxmsg = buffer_info.nbuffers + 4;
  attributes.mq_msgsize = sizeof(struct audio_msg_s);
  printf("[audio_record] mq open %s maxmsg=%ld msgsize=%ld\n",
         capture->mqname, attributes.mq_maxmsg, attributes.mq_msgsize);
  capture->mq = mq_open(capture->mqname, O_RDWR | O_CREAT, 0644,
                        &attributes);
  if (capture->mq == (mqd_t)-1)
    {
      ret = -errno;
      fprintf(stderr, "[audio_record] mq_open failed: %d\n", ret);
      goto fail;
    }

  if (ioctl(capture->fd, AUDIOIOC_REGISTERMQ,
            (unsigned long)capture->mq) < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio_record] register mq failed: %d\n", ret);
      goto fail;
    }

  for (i = 0; i < buffer_info.nbuffers; i++)
    {
      struct audio_buf_desc_s descriptor;
      int ioctl_ret;

      memset(&descriptor, 0, sizeof(descriptor));
#ifdef CONFIG_AUDIO_MULTI_SESSION
      descriptor.session = capture->session;
#endif
      descriptor.numbytes = buffer_info.buffer_size;
      descriptor.u.pbuffer = &capture->buffers[i];

      if (capture->use_local_buffers)
        {
          ioctl_ret = apb_alloc(&descriptor);
        }
      else
        {
          ioctl_ret = ioctl(capture->fd, AUDIOIOC_ALLOCBUFFER,
                            (unsigned long)(uintptr_t)&descriptor);
        }

      if (ioctl_ret != sizeof(descriptor))
        {
          ret = ioctl_ret < 0 ? ioctl_ret : -ENOMEM;
          fprintf(stderr,
                  "[audio_record] alloc buffer %u failed: ret=%d "
                  "status=%d\n", i + 1, ioctl_ret, ret);
          goto fail;
        }

      capture->buffer_count++;
      ret = enqueue_buffer(capture, capture->buffers[i]);
      if (ret < 0)
        {
          fprintf(stderr, "[audio_record] enqueue buffer %u failed: %d\n",
                  i + 1, ret);
          goto fail;
        }
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  ret = ioctl(capture->fd, AUDIOIOC_START,
              (unsigned long)(uintptr_t)capture->session);
#else
  ret = ioctl(capture->fd, AUDIOIOC_START, 0);
#endif
  if (ret < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio_record] start failed: %d\n", ret);
      goto fail;
    }

  capture->started = true;
  printf("[audio_record] capturing %s at %u Hz, device=%uch/%u-bit, "
         "wav=mono/16-bit\n", options->device, options->rate,
         AUDIO_RECORD_DEVICE_CHANNELS, AUDIO_RECORD_DEVICE_BITS);
  return 0;

fail:
  capture_deinit(capture);
  return ret;
}

static ssize_t capture_read_mono(struct audio_record_capture_s *capture,
                                 const struct audio_record_options_s *options,
                                 int16_t *samples,
                                 size_t sample_count)
{
  size_t copied = 0;

  if (capture->fd < 0 || !capture->started || samples == NULL ||
      sample_count == 0)
    {
      return -EINVAL;
    }

  while (copied < sample_count)
    {
      size_t available;
      size_t take;

      if (capture->current == NULL ||
          capture->current_offset >= capture->current->nbytes)
        {
          int ret;

          if (capture->current != NULL)
            {
              ret = enqueue_buffer(capture, capture->current);
              capture->current = NULL;
              if (ret < 0)
                {
                  return copied > 0 ? (ssize_t)copied : ret;
                }
            }

          ret = wait_for_buffer(capture);
          if (ret < 0)
            {
              return copied > 0 ? (ssize_t)copied : ret;
            }
        }

#ifdef CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_32BIT
      available = (capture->current->nbytes - capture->current_offset) /
                  AUDIO_RECORD_FRAME_BYTES;
#else
      available = (capture->current->nbytes - capture->current_offset) /
                  sizeof(int16_t);
#endif
      if (available == 0)
        {
          capture->current_offset = capture->current->nbytes;
          continue;
        }

      take = sample_count - copied;
      if (take > available)
        {
          take = available;
        }

#ifdef CONFIG_EXAMPLES_AUDIO_RECORD_INMP441_32BIT
      {
        size_t i;
        const uint8_t *cursor = capture->current->samp +
                                capture->current_offset;

        for (i = 0; i < take; i++)
          {
            samples[copied + i] = convert_inmp441_sample(cursor,
                                                         options->slot,
                                                         options->shift);
            cursor += AUDIO_RECORD_FRAME_BYTES;
          }
      }

      capture->current_offset += take * AUDIO_RECORD_FRAME_BYTES;
#else
      memcpy(samples + copied,
             capture->current->samp + capture->current_offset,
             take * sizeof(int16_t));
      capture->current_offset += take * sizeof(int16_t);
#endif
      copied += take;
    }

  return copied;
}

static void put_le16(uint8_t *buffer, uint16_t value)
{
  buffer[0] = value & 0xff;
  buffer[1] = value >> 8;
}

static void put_le32(uint8_t *buffer, uint32_t value)
{
  buffer[0] = value & 0xff;
  buffer[1] = (value >> 8) & 0xff;
  buffer[2] = (value >> 16) & 0xff;
  buffer[3] = value >> 24;
}

static void wav_header(uint8_t header[AUDIO_RECORD_WAV_HEADER_SIZE],
                       unsigned int rate,
                       uint32_t data_bytes)
{
  uint32_t byte_rate = rate * AUDIO_RECORD_OUTPUT_CHANNELS *
                       (AUDIO_RECORD_OUTPUT_BITS / 8);
  uint16_t block_align = AUDIO_RECORD_OUTPUT_CHANNELS *
                         (AUDIO_RECORD_OUTPUT_BITS / 8);

  memcpy(header + 0, "RIFF", 4);
  put_le32(header + 4, 36 + data_bytes);
  memcpy(header + 8, "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  put_le32(header + 16, 16);
  put_le16(header + 20, 1);
  put_le16(header + 22, AUDIO_RECORD_OUTPUT_CHANNELS);
  put_le32(header + 24, rate);
  put_le32(header + 28, byte_rate);
  put_le16(header + 32, block_align);
  put_le16(header + 34, AUDIO_RECORD_OUTPUT_BITS);
  memcpy(header + 36, "data", 4);
  put_le32(header + 40, data_bytes);
}

static void b64_putchar(struct audio_record_b64_s *state, char ch)
{
  putchar(ch);
  state->column++;

  if (state->column >= AUDIO_RECORD_B64_COLUMNS)
    {
      putchar('\n');
      state->column = 0;
    }
}

static void b64_emit(struct audio_record_b64_s *state,
                     const uint8_t input[3],
                     unsigned int count)
{
  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint32_t value = ((uint32_t)input[0] << 16) |
                   ((uint32_t)input[1] << 8) |
                   input[2];

  b64_putchar(state, table[(value >> 18) & 0x3f]);
  b64_putchar(state, table[(value >> 12) & 0x3f]);
  b64_putchar(state, count > 1 ? table[(value >> 6) & 0x3f] : '=');
  b64_putchar(state, count > 2 ? table[value & 0x3f] : '=');
}

static void b64_update(struct audio_record_b64_s *state,
                       const uint8_t *data,
                       size_t len)
{
  while (len > 0)
    {
      state->pending[state->pending_len++] = *data++;
      len--;

      if (state->pending_len == 3)
        {
          b64_emit(state, state->pending, 3);
          state->pending_len = 0;
        }
    }
}

static void b64_finish(struct audio_record_b64_s *state)
{
  if (state->pending_len > 0)
    {
      unsigned int count = state->pending_len;

      while (state->pending_len < 3)
        {
          state->pending[state->pending_len++] = 0;
        }

      b64_emit(state, state->pending, count);
    }

  if (state->column != 0)
    {
      putchar('\n');
    }
}

static void export_wav_base64(const int16_t *samples,
                              unsigned int sample_count,
                              unsigned int rate)
{
  struct audio_record_b64_s b64;
  uint8_t header[AUDIO_RECORD_WAV_HEADER_SIZE];
  uint32_t data_bytes = sample_count * sizeof(int16_t);
  unsigned int i;

  memset(&b64, 0, sizeof(b64));
  wav_header(header, rate, data_bytes);

  printf("WAV_BASE64_BEGIN\n");
  b64_update(&b64, header, sizeof(header));

  for (i = 0; i < sample_count; i++)
    {
      uint8_t encoded[2];
      encoded[0] = (uint16_t)samples[i] & 0xff;
      encoded[1] = ((uint16_t)samples[i] >> 8) & 0xff;
      b64_update(&b64, encoded, sizeof(encoded));
    }

  b64_finish(&b64);
  printf("WAV_BASE64_END\n");
}

int audio_record_main(int argc, char *argv[])
{
  struct audio_record_options_s options;
  struct audio_record_capture_s capture;
  struct audio_record_stats_s stats;
  int16_t *recording = NULL;
  unsigned int total_samples;
  unsigned int captured_samples = 0;
  size_t record_bytes;
  int ret;

  ret = parse_options(argc, argv, &options);
  if (ret > 0)
    {
      return 0;
    }

  if (ret < 0)
    {
      audio_record_usage();
      return 1;
    }

  total_samples = options.rate * options.seconds;
  record_bytes = (size_t)total_samples * sizeof(int16_t);
  recording = (int16_t *)malloc(record_bytes);
  if (recording == NULL)
    {
      fprintf(stderr, "[audio_record] cannot allocate %u bytes; "
              "enable PSRAM common heap for long recordings\n",
              (unsigned int)record_bytes);
      return 1;
    }

  memset(&stats, 0, sizeof(stats));
  stats.min = INT16_MAX;
  stats.max = INT16_MIN;

  printf("[audio_record] record %u seconds, %u Hz, mono int16, "
         "%u samples, %u bytes\n",
         options.seconds, options.rate, total_samples,
         (unsigned int)record_bytes);
  printf("[audio_record] recording buffer=%p\n", recording);

  ret = capture_init(&capture, &options);
  if (ret < 0)
    {
      free(recording);
      return 1;
    }

  while (captured_samples < total_samples)
    {
      unsigned int want = total_samples - captured_samples;
      ssize_t got;

      if (want > options.block_frames)
        {
          want = options.block_frames;
        }

      got = capture_read_mono(&capture, &options, g_read_buffer, want);
      if (got < 0)
        {
          fprintf(stderr, "[audio_record] capture failed: %d\n", (int)got);
          capture_deinit(&capture);
          free(recording);
          return 1;
        }

      if (got == 0)
        {
          continue;
        }

      memcpy(recording + captured_samples, g_read_buffer,
             (size_t)got * sizeof(int16_t));
      stats_update(&stats, g_read_buffer, (unsigned int)got);
      captured_samples += (unsigned int)got;
    }

  capture_deinit(&capture);
  stats_print(&stats);

  printf("[audio_record] exporting WAV as base64; copy text between "
         "markers to record.b64\n");
  export_wav_base64(recording, captured_samples, options.rate);
  printf("[audio_record] host decode: base64 -d record.b64 > record.wav\n");

  free(recording);
  return 0;
}
