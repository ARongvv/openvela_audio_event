/*
 * PCM/WAV file input for deterministic simulator tests.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "audio/audio_file.h"
#include "audio_event_config.h"

#define FILE_READ_MAX_SAMPLES 512

static uint16_t read_le16(const uint8_t *value)
{
  return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t read_le32(const uint8_t *value)
{
  return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
         ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int read_exact(int fd, void *buffer, size_t size)
{
  uint8_t *cursor = buffer;

  while (size > 0)
    {
      ssize_t count = read(fd, cursor, size);
      if (count == 0)
        {
          return -ENODATA;
        }

      if (count < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return -errno;
        }

      cursor += count;
      size -= count;
    }

  return 0;
}

static bool has_suffix(const char *path, const char *suffix)
{
  size_t path_length = strlen(path);
  size_t suffix_length = strlen(suffix);

  return path_length >= suffix_length &&
         strcmp(path + path_length - suffix_length, suffix) == 0;
}

static int parse_wav(struct audio_file_s *source)
{
  uint8_t header[12];
  bool format_found = false;

  int ret = read_exact(source->fd, header, sizeof(header));
  if (ret < 0 || memcmp(header, "RIFF", 4) != 0 ||
      memcmp(header + 8, "WAVE", 4) != 0)
    {
      return -EINVAL;
    }

  for (;;)
    {
      uint8_t chunk[8];
      uint32_t chunk_size;

      ret = read_exact(source->fd, chunk, sizeof(chunk));
      if (ret < 0)
        {
          return ret;
        }

      chunk_size = read_le32(chunk + 4);
      if (memcmp(chunk, "fmt ", 4) == 0)
        {
          uint8_t format[16];
          if (chunk_size < sizeof(format) ||
              read_exact(source->fd, format, sizeof(format)) < 0)
            {
              return -EINVAL;
            }

          if (read_le16(format) != 1 || read_le16(format + 2) != 1 ||
              read_le32(format + 4) != AUDIO_EVENT_SAMPLE_RATE ||
              read_le16(format + 14) != 16)
            {
              fprintf(stderr,
                      "[file] WAV must be PCM, 16 kHz, mono, 16-bit\n");
              return -ENOTSUP;
            }

          if (chunk_size > sizeof(format) &&
              lseek(source->fd, chunk_size - sizeof(format), SEEK_CUR) < 0)
            {
              return -errno;
            }

          format_found = true;
        }
      else if (memcmp(chunk, "data", 4) == 0)
        {
          if (!format_found || (chunk_size & 1) != 0)
            {
              return -EINVAL;
            }

          source->data_offset = lseek(source->fd, 0, SEEK_CUR);
          source->data_size = chunk_size;
          return source->data_offset < 0 ? -errno : 0;
        }
      else if (lseek(source->fd, chunk_size, SEEK_CUR) < 0)
        {
          return -errno;
        }

      if ((chunk_size & 1) != 0 && lseek(source->fd, 1, SEEK_CUR) < 0)
        {
          return -errno;
        }
    }
}

int audio_file_open(struct audio_file_s *source, const char *path,
                    unsigned int repeat_count)
{
  off_t end;
  int ret;

  if (source == NULL || path == NULL || repeat_count == 0)
    {
      return -EINVAL;
    }

  memset(source, 0, sizeof(*source));
  source->fd = open(path, O_RDONLY);
  if (source->fd < 0)
    {
      return -errno;
    }

  source->repeats_left = repeat_count;
  if (has_suffix(path, ".wav") || has_suffix(path, ".WAV"))
    {
      ret = parse_wav(source);
      if (ret < 0)
        {
          audio_file_close(source);
          return ret;
        }
    }
  else
    {
      end = lseek(source->fd, 0, SEEK_END);
      if (end < 0 || lseek(source->fd, 0, SEEK_SET) < 0)
        {
          ret = -errno;
          audio_file_close(source);
          return ret;
        }

      source->data_offset = 0;
      source->data_size = end;
      if ((source->data_size & 1) != 0)
        {
          audio_file_close(source);
          return -EINVAL;
        }
    }

  source->data_read = 0;
  printf("[file] opened %s, samples=%zu repeat=%u\n", path,
         source->data_size / sizeof(int16_t), repeat_count);
  return 0;
}

ssize_t audio_file_read(struct audio_file_s *source, int16_t *samples,
                        size_t sample_count)
{
  uint8_t bytes[FILE_READ_MAX_SAMPLES * sizeof(int16_t)];
  size_t wanted;
  size_t i;

  if (source == NULL || source->fd < 0 || samples == NULL ||
      sample_count == 0 || sample_count > FILE_READ_MAX_SAMPLES)
    {
      return -EINVAL;
    }

  if (source->data_read == source->data_size)
    {
      if (source->repeats_left <= 1)
        {
          return 0;
        }

      source->repeats_left--;
      source->data_read = 0;
      if (lseek(source->fd, source->data_offset, SEEK_SET) < 0)
        {
          return -errno;
        }
    }

  wanted = sample_count * sizeof(int16_t);
  if (wanted > source->data_size - source->data_read)
    {
      wanted = source->data_size - source->data_read;
    }

  if (read_exact(source->fd, bytes, wanted) < 0)
    {
      return -EIO;
    }

  source->data_read += wanted;
  for (i = 0; i < wanted / sizeof(int16_t); i++)
    {
      samples[i] = (int16_t)read_le16(&bytes[i * sizeof(int16_t)]);
    }

  return wanted / sizeof(int16_t);
}

void audio_file_close(struct audio_file_s *source)
{
  if (source != NULL && source->fd >= 0)
    {
      close(source->fd);
      source->fd = -1;
    }
}
