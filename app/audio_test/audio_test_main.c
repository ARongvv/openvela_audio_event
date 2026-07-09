/*
 * Audio capture diagnostic command.
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

#ifndef CONFIG_EXAMPLES_AUDIO_TEST_DEVPATH
#  define CONFIG_EXAMPLES_AUDIO_TEST_DEVPATH "/dev/audio/pcm_in1"
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_TEST_SAMPLE_RATE
#  define CONFIG_EXAMPLES_AUDIO_TEST_SAMPLE_RATE 16000
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_TEST_CHANNELS
#  define CONFIG_EXAMPLES_AUDIO_TEST_CHANNELS 1
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_TEST_BLOCK_FRAMES
#  define CONFIG_EXAMPLES_AUDIO_TEST_BLOCK_FRAMES 512
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_SLOT
#  define CONFIG_EXAMPLES_AUDIO_TEST_INMP441_SLOT 0
#endif

#ifndef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_SHIFT
#  define CONFIG_EXAMPLES_AUDIO_TEST_INMP441_SHIFT 16
#endif

#ifndef CONFIG_AUDIO_NUM_BUFFERS
#  define CONFIG_AUDIO_NUM_BUFFERS 4
#endif

#ifndef CONFIG_AUDIO_BUFFER_NUMBYTES
#  define CONFIG_AUDIO_BUFFER_NUMBYTES 2048
#endif

#define AUDIO_TEST_DEFAULT_SECONDS 5
#define AUDIO_TEST_MAX_CHANNELS 2
#define AUDIO_TEST_MAX_REPORT_CHANNELS 3
#define AUDIO_TEST_MAX_BUFFERS 8
#define AUDIO_TEST_FALLBACK_MAX_BUFFERS 4
#define AUDIO_TEST_FALLBACK_MAX_BYTES 2048
#define AUDIO_TEST_MAX_BLOCK_FRAMES 1024
#define AUDIO_TEST_NEAR_CLIP_LEVEL 32000

#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
#  define AUDIO_TEST_DEVICE_CHANNELS 2
#  define AUDIO_TEST_DEVICE_BITS 32
#  define AUDIO_TEST_SLOT_BYTES 4
#  define AUDIO_TEST_FRAME_BYTES \
    (AUDIO_TEST_DEVICE_CHANNELS * AUDIO_TEST_SLOT_BYTES)
#  define AUDIO_TEST_REPORT_CHANNELS AUDIO_TEST_MAX_REPORT_CHANNELS
#else
#  define AUDIO_TEST_DEVICE_BITS 16
#endif

struct audio_test_options_s
{
  const char *device;
  unsigned int rate;
  unsigned int channels;
  unsigned int seconds;
  unsigned int block_frames;
  unsigned int slot;
  unsigned int shift;
};

struct audio_test_capture_s
{
  int fd;
  mqd_t mq;
  char mqname[32];
  struct ap_buffer_s *buffers[AUDIO_TEST_MAX_BUFFERS];
  unsigned int buffer_count;
  struct ap_buffer_s *current;
  size_t current_offset;
  bool started;
  bool use_local_buffers;
#ifdef CONFIG_AUDIO_MULTI_SESSION
  void *session;
#endif
};

struct audio_test_stats_s
{
  int16_t min;
  int16_t max;
  int64_t sum;
  uint64_t sumsq;
  unsigned int zero_count;
  unsigned int clip_count;
  unsigned int near_clip_count;
  unsigned int samples;
  bool initialized;
};

static int16_t g_read_buffer[AUDIO_TEST_MAX_BLOCK_FRAMES *
                             AUDIO_TEST_MAX_REPORT_CHANNELS];

static void audio_test_usage(void)
{
  printf("Usage: audio_test [options]\n");
  printf("Options:\n");
  printf("  --device PATH        audio input device, default %s\n",
         CONFIG_EXAMPLES_AUDIO_TEST_DEVPATH);
  printf("  --rate HZ           sample rate, default %d\n",
         CONFIG_EXAMPLES_AUDIO_TEST_SAMPLE_RATE);
#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
  printf("  --channels N        ignored in INMP441 mode; device uses 2ch/32-bit\n");
#else
  printf("  --channels N        channel count 1 or 2, default %d\n",
         CONFIG_EXAMPLES_AUDIO_TEST_CHANNELS);
#endif
  printf("  --seconds N         capture duration, default %d\n",
         AUDIO_TEST_DEFAULT_SECONDS);
  printf("  --block-frames N    frames per read 1..%d, default %d\n",
         AUDIO_TEST_MAX_BLOCK_FRAMES,
         CONFIG_EXAMPLES_AUDIO_TEST_BLOCK_FRAMES);
#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
  printf("  --slot N            INMP441 slot 0 or 1, default %d\n",
         CONFIG_EXAMPLES_AUDIO_TEST_INMP441_SLOT);
  printf("  --shift N           int32 to int16 right shift 0..24, default %d\n",
         CONFIG_EXAMPLES_AUDIO_TEST_INMP441_SHIFT);
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
                         struct audio_test_options_s *options)
{
  int i;

  options->device = CONFIG_EXAMPLES_AUDIO_TEST_DEVPATH;
  options->rate = CONFIG_EXAMPLES_AUDIO_TEST_SAMPLE_RATE;
  options->channels = CONFIG_EXAMPLES_AUDIO_TEST_CHANNELS;
  options->seconds = AUDIO_TEST_DEFAULT_SECONDS;
  options->block_frames = CONFIG_EXAMPLES_AUDIO_TEST_BLOCK_FRAMES;
  options->slot = CONFIG_EXAMPLES_AUDIO_TEST_INMP441_SLOT;
  options->shift = CONFIG_EXAMPLES_AUDIO_TEST_INMP441_SHIFT;

  for (i = 1; i < argc; i++)
    {
      const char *arg = argv[i];
      unsigned int value;

      if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
        {
          audio_test_usage();
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
      else if (strcmp(arg, "--channels") == 0)
        {
          if (++i >= argc || parse_uint_arg(argv[i], &value) < 0)
            {
              return -EINVAL;
            }

          options->channels = value;
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
          fprintf(stderr, "[audio_test] unknown option: %s\n", arg);
          return -EINVAL;
        }
    }

  if (options->device == NULL || options->device[0] == '\0' ||
      options->rate == 0 ||
      options->channels == 0 ||
      options->channels > AUDIO_TEST_MAX_CHANNELS ||
      options->seconds == 0 ||
      options->block_frames == 0 ||
      options->block_frames > AUDIO_TEST_MAX_BLOCK_FRAMES ||
      options->slot > 1 ||
      options->shift > 24)
    {
      return -EINVAL;
    }

#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
  options->channels = AUDIO_TEST_DEVICE_CHANNELS;
#endif

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

static void stats_reset(struct audio_test_stats_s stats[],
                        unsigned int channels)
{
  unsigned int i;

  for (i = 0; i < channels; i++)
    {
      memset(&stats[i], 0, sizeof(stats[i]));
      stats[i].min = INT16_MAX;
      stats[i].max = INT16_MIN;
    }
}

static void stats_update(struct audio_test_stats_s stats[],
                         unsigned int channels,
                         const int16_t *samples,
                         unsigned int frames)
{
  unsigned int frame;

  for (frame = 0; frame < frames; frame++)
    {
      unsigned int ch;

      for (ch = 0; ch < channels; ch++)
        {
          int16_t sample = samples[frame * channels + ch];
          struct audio_test_stats_s *st = &stats[ch];
          int32_t widened = sample;

          if (!st->initialized)
            {
              st->min = sample;
              st->max = sample;
              st->initialized = true;
            }
          else
            {
              if (sample < st->min)
                {
                  st->min = sample;
                }

              if (sample > st->max)
                {
                  st->max = sample;
                }
            }

          st->sum += sample;
          st->sumsq += (uint64_t)(widened * widened);
          st->samples++;

          if (sample == 0)
            {
              st->zero_count++;
            }

          if (sample == INT16_MIN || sample == INT16_MAX)
            {
              st->clip_count++;
            }

          if ((widened < 0 ? -widened : widened) >=
              AUDIO_TEST_NEAR_CLIP_LEVEL)
            {
              st->near_clip_count++;
            }
        }
    }
}

static const char *stats_label(unsigned int ch,
                               const struct audio_test_options_s *options)
{
#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
  static char mono_label[16];

  if (ch == 0)
    {
      return "slot0";
    }

  if (ch == 1)
    {
      return "slot1";
    }

  snprintf(mono_label, sizeof(mono_label), "mono(slot%u)",
           options->slot);
  return mono_label;
#else
  static char channel_label[8];

  snprintf(channel_label, sizeof(channel_label), "ch%u", ch);
  return channel_label;
#endif
}

static void stats_print(unsigned int second,
                        const struct audio_test_stats_s stats[],
                        const struct audio_test_options_s *options,
                        unsigned int channels,
                        unsigned int frames)
{
  unsigned int ch;

  printf("[audio_test] second=%u frames=%u\n", second, frames);

  for (ch = 0; ch < channels; ch++)
    {
      const struct audio_test_stats_s *st = &stats[ch];
      int64_t mean = 0;
      uint32_t rms = 0;

      if (st->samples > 0)
        {
          mean = st->sum / st->samples;
          rms = isqrt_u64(st->sumsq / st->samples);
        }

      printf("[audio_test] %s min=%d max=%d mean=%lld rms=%lu "
             "zero=%u/%u clip=%u nearclip=%u\n",
             stats_label(ch, options),
             st->initialized ? st->min : 0,
             st->initialized ? st->max : 0,
             (long long)mean, (unsigned long)rms,
             st->zero_count, st->samples, st->clip_count,
             st->near_clip_count);
    }
}

#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
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
  const uint8_t *slot = frame + slot_index * AUDIO_TEST_SLOT_BYTES;
  int32_t raw = load_le32(slot);
  int32_t shifted = raw >> shift;

  return clamp_int16(shifted);
}
#endif

static int enqueue_buffer(struct audio_test_capture_s *capture,
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

static int wait_for_buffer(struct audio_test_capture_s *capture)
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

static void capture_deinit(struct audio_test_capture_s *capture)
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

static int capture_init(struct audio_test_capture_s *capture,
                        const struct audio_test_options_s *options)
{
  struct audio_caps_desc_s capabilities;
  struct ap_buffer_info_s buffer_info;
  struct mq_attr attributes;
  unsigned int i;
  int ret;

  memset(capture, 0, sizeof(*capture));
  capture->fd = -1;
  capture->mq = (mqd_t)-1;

  printf("[audio_test] open %s\n", options->device);
  capture->fd = open(options->device, O_RDWR | O_CLOEXEC);
  if (capture->fd < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio_test] cannot open %s: %d\n",
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
      fprintf(stderr, "[audio_test] reserve failed: %d\n", ret);
      goto fail;
    }

  memset(&capabilities, 0, sizeof(capabilities));
#ifdef CONFIG_AUDIO_MULTI_SESSION
  capabilities.session = capture->session;
#endif
  capabilities.caps.ac_len = sizeof(struct audio_caps_s);
  capabilities.caps.ac_type = AUDIO_TYPE_INPUT;
  capabilities.caps.ac_subtype = AUDIO_FMT_PCM;
#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
  capabilities.caps.ac_channels = AUDIO_TEST_DEVICE_CHANNELS;
#else
  capabilities.caps.ac_channels = options->channels;
#endif
  capabilities.caps.ac_controls.hw[0] = options->rate & 0xffff;
  capabilities.caps.ac_controls.b[3] = options->rate >> 16;
  capabilities.caps.ac_controls.b[2] = AUDIO_TEST_DEVICE_BITS;

  printf("[audio_test] configure input pcm rate=%u channels=%u bits=%u\n",
         options->rate, capabilities.caps.ac_channels, AUDIO_TEST_DEVICE_BITS);
#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
  printf("[audio_test] INMP441 adapter: slot=%u shift=%u "
         "output=mono int16; reporting slot0/slot1/mono\n",
         options->slot, options->shift);
#endif
  if (ioctl(capture->fd, AUDIOIOC_CONFIGURE,
            (unsigned long)(uintptr_t)&capabilities) < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio_test] configure failed: %d\n", ret);
      goto fail;
    }

  memset(&buffer_info, 0, sizeof(buffer_info));
  if (ioctl(capture->fd, AUDIOIOC_GETBUFFERINFO,
            (unsigned long)(uintptr_t)&buffer_info) < 0)
    {
      ret = -errno;
      printf("[audio_test] get buffer info failed: %d, "
             "fallback to local buffers\n", ret);
      buffer_info.nbuffers = CONFIG_AUDIO_NUM_BUFFERS;
      buffer_info.buffer_size = CONFIG_AUDIO_BUFFER_NUMBYTES;

      if (buffer_info.nbuffers > AUDIO_TEST_FALLBACK_MAX_BUFFERS)
        {
          buffer_info.nbuffers = AUDIO_TEST_FALLBACK_MAX_BUFFERS;
        }

      if (buffer_info.buffer_size > AUDIO_TEST_FALLBACK_MAX_BYTES)
        {
          buffer_info.buffer_size = AUDIO_TEST_FALLBACK_MAX_BYTES;
        }

      capture->use_local_buffers = true;
    }

  printf("[audio_test] buffer info: nbuffers=%u size=%u bytes\n",
         buffer_info.nbuffers, buffer_info.buffer_size);
  if (buffer_info.nbuffers == 0 ||
      buffer_info.nbuffers > AUDIO_TEST_MAX_BUFFERS)
    {
      ret = -E2BIG;
      fprintf(stderr, "[audio_test] unsupported buffer count: %u max=%u\n",
              buffer_info.nbuffers, AUDIO_TEST_MAX_BUFFERS);
      goto fail;
    }

  snprintf(capture->mqname, sizeof(capture->mqname), "/audio_test_%ld",
           (long)getpid());
  memset(&attributes, 0, sizeof(attributes));
  attributes.mq_maxmsg = buffer_info.nbuffers + 4;
  attributes.mq_msgsize = sizeof(struct audio_msg_s);
  printf("[audio_test] mq open %s maxmsg=%ld msgsize=%ld\n",
         capture->mqname, attributes.mq_maxmsg, attributes.mq_msgsize);
  capture->mq = mq_open(capture->mqname, O_RDWR | O_CREAT, 0644,
                        &attributes);
  if (capture->mq == (mqd_t)-1)
    {
      ret = -errno;
      fprintf(stderr, "[audio_test] mq_open failed: %d\n", ret);
      goto fail;
    }

  if (ioctl(capture->fd, AUDIOIOC_REGISTERMQ,
            (unsigned long)capture->mq) < 0)
    {
      ret = -errno;
      fprintf(stderr, "[audio_test] register mq failed: %d\n", ret);
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
                  "[audio_test] alloc buffer %u failed: ret=%d status=%d\n",
                  i + 1, ioctl_ret, ret);
          goto fail;
        }

      capture->buffer_count++;
      ret = enqueue_buffer(capture, capture->buffers[i]);
      if (ret < 0)
        {
          fprintf(stderr, "[audio_test] enqueue buffer %u failed: %d\n",
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
      fprintf(stderr, "[audio_test] start failed: %d\n", ret);
      goto fail;
    }

  capture->started = true;
#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
  printf("[audio_test] capturing %s at %u Hz, device=2ch/32-bit, "
         "report=slot0/slot1/mono(slot%u)\n",
         options->device, options->rate, options->slot);
#else
  printf("[audio_test] capturing %s at %u Hz, channels=%u, 16-bit\n",
         options->device, options->rate, options->channels);
#endif
  return 0;

fail:
  capture_deinit(capture);
  return ret;
}

static ssize_t capture_read_frames(struct audio_test_capture_s *capture,
                                   const struct audio_test_options_s *options,
                                   int16_t *samples,
                                   size_t frame_count)
{
  size_t copied_frames = 0;
#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
  const size_t input_frame_bytes = AUDIO_TEST_FRAME_BYTES;
#else
  const size_t input_frame_bytes = options->channels * sizeof(int16_t);
#endif

  if (capture->fd < 0 || !capture->started || samples == NULL ||
      frame_count == 0)
    {
      return -EINVAL;
    }

  while (copied_frames < frame_count)
    {
      size_t available_frames;
      size_t take_frames;

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
                  return copied_frames > 0 ? (ssize_t)copied_frames : ret;
                }
            }

          ret = wait_for_buffer(capture);
          if (ret < 0)
            {
              return copied_frames > 0 ? (ssize_t)copied_frames : ret;
            }
        }

      available_frames = (capture->current->nbytes -
                          capture->current_offset) / input_frame_bytes;
      take_frames = frame_count - copied_frames;
      if (take_frames > available_frames)
        {
          take_frames = available_frames;
        }

      if (take_frames == 0)
        {
          capture->current_offset = capture->current->nbytes;
          continue;
        }

#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
      {
        const uint8_t *src = capture->current->samp +
                             capture->current_offset;
        size_t frame;

        for (frame = 0; frame < take_frames; frame++)
          {
            const uint8_t *input = src + frame * input_frame_bytes;
            int16_t slot0 = convert_inmp441_sample(input, 0,
                                                   options->shift);
            int16_t slot1 = convert_inmp441_sample(input, 1,
                                                   options->shift);
            int16_t mono = options->slot == 0 ? slot0 : slot1;
            size_t out = (copied_frames + frame) *
                         AUDIO_TEST_REPORT_CHANNELS;

            samples[out] = slot0;
            samples[out + 1] = slot1;
            samples[out + 2] = mono;
          }
      }
#else
      memcpy(samples + copied_frames * options->channels,
             capture->current->samp + capture->current_offset,
             take_frames * input_frame_bytes);
#endif
      copied_frames += take_frames;
      capture->current_offset += take_frames * input_frame_bytes;
    }

  return (ssize_t)copied_frames;
}

int audio_test_main(int argc, char *argv[])
{
  struct audio_test_options_s options;
  struct audio_test_capture_s capture;
  struct audio_test_stats_s stats[AUDIO_TEST_MAX_REPORT_CHANNELS];
  unsigned int report_channels;
  unsigned int second;
  int ret;

  ret = parse_options(argc, argv, &options);
  if (ret > 0)
    {
      return 0;
    }

  if (ret < 0)
    {
      audio_test_usage();
      return EXIT_FAILURE;
    }

  ret = capture_init(&capture, &options);
  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

#ifdef CONFIG_EXAMPLES_AUDIO_TEST_INMP441_32BIT
  report_channels = AUDIO_TEST_REPORT_CHANNELS;
#else
  report_channels = options.channels;
#endif

  for (second = 0; second < options.seconds; second++)
    {
      unsigned int frames_done = 0;

      stats_reset(stats, report_channels);

      while (frames_done < options.rate)
        {
          unsigned int frames_left = options.rate - frames_done;
          unsigned int frames_to_read = options.block_frames;
          unsigned int frames_read;
          ssize_t read_ret;

          if (frames_to_read > frames_left)
            {
              frames_to_read = frames_left;
            }

          read_ret = capture_read_frames(&capture, &options, g_read_buffer,
                                         frames_to_read);
          if (read_ret < 0)
            {
              fprintf(stderr, "[audio_test] capture read failed: %d\n",
                      (int)read_ret);
              capture_deinit(&capture);
              return EXIT_FAILURE;
            }

          frames_read = (unsigned int)read_ret;
          if (frames_read == 0)
            {
              fprintf(stderr, "[audio_test] capture read returned no frame\n");
              capture_deinit(&capture);
              return EXIT_FAILURE;
            }

          stats_update(stats, report_channels, g_read_buffer, frames_read);
          frames_done += frames_read;
        }

      stats_print(second + 1, stats, &options, report_channels, frames_done);
    }

  capture_deinit(&capture);
  return EXIT_SUCCESS;
}
