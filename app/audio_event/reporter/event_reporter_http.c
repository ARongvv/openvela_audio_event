/*
 * HTTP POST backend for asynchronous audio-event reporting.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "reporter/event_reporter.h"

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_REPORT
#include <netutils/webclient.h>

#define REPORTER_HTTP_BUFFER_SIZE 512
#define REPORTER_HTTP_BODY_SIZE   256
#define REPORTER_HTTP_AUTH_SIZE   160
#define REPORTER_DEVICE_ID_SIZE   48

static void reporter_copy_json_token(char *destination, size_t destination_size,
                                     const char *source)
{
  size_t index;

  if (destination_size == 0)
    {
      return;
    }

  for (index = 0; index + 1 < destination_size && source[index] != '\0';
       index++)
    {
      char value = source[index];

      if ((value >= 'a' && value <= 'z') ||
          (value >= 'A' && value <= 'Z') ||
          (value >= '0' && value <= '9') || value == '_' || value == '-' ||
          value == '.')
        {
          destination[index] = value;
        }
      else
        {
          destination[index] = '_';
        }
    }

  destination[index] = '\0';
}

int event_reporter_http_post(const struct report_event_s *event,
                             unsigned int *http_status)
{
  static const char *const basic_headers[] =
  {
    "Content-Type: application/json",
    "Connection: close"
  };
  struct webclient_context context;
  const char *headers[3];
  char buffer[REPORTER_HTTP_BUFFER_SIZE];
  char body[REPORTER_HTTP_BODY_SIZE];
  char authorization[REPORTER_HTTP_AUTH_SIZE];
  char device_id[REPORTER_DEVICE_ID_SIZE];
  int length;
  int ret;
  unsigned int timeout_seconds;

  if (http_status != NULL)
    {
      *http_status = 0;
    }

  if (event == NULL || CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_HTTP_ENDPOINT[0] == '\0')
    {
      return -EINVAL;
    }

  reporter_copy_json_token(device_id, sizeof(device_id),
                           CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_DEVICE_ID);
  length = snprintf(body, sizeof(body),
                    "{\"schema_version\":1,\"event_id\":\"%s-%08lx-%lu\","
                    "\"device_id\":\"%s\",\"event\":\"%s\","
                    "\"confidence_permille\":%u,\"monotonic_ms\":%llu}",
                    device_id, (unsigned long)event->boot_id,
                    (unsigned long)event->sequence, device_id,
                    event->event_name, event->confidence_permille,
                    (unsigned long long)event->monotonic_ms);
  if (length < 0 || length >= (int)sizeof(body))
    {
      return -ENAMETOOLONG;
    }

  headers[0] = basic_headers[0];
  headers[1] = basic_headers[1];
  headers[2] = NULL;
  if (CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_HTTP_TOKEN[0] != '\0')
    {
      length = snprintf(authorization, sizeof(authorization),
                        "Authorization: Bearer %s",
                        CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_HTTP_TOKEN);
      if (length < 0 || length >= (int)sizeof(authorization))
        {
          return -ENAMETOOLONG;
        }

      headers[2] = authorization;
    }

  webclient_set_defaults(&context);
  timeout_seconds =
      (CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_HTTP_TIMEOUT_MS + 999) / 1000;
  context.protocol_version = WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1;
  context.method = "POST";
  context.url = CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_HTTP_ENDPOINT;
  context.headers = headers;
  context.nheaders = headers[2] == NULL ? 2 : 3;
  context.buffer = buffer;
  context.buflen = sizeof(buffer);
  context.timeout_sec = timeout_seconds == 0 ? 1 : timeout_seconds;
  webclient_set_static_body(&context, body, (size_t)length);

  ret = webclient_perform(&context);
  if (http_status != NULL)
    {
      *http_status = context.http_status;
    }

  if (ret < 0)
    {
      return ret;
    }

  if (context.http_status < 200 || context.http_status >= 300)
    {
      return -EIO;
    }

  return 0;
}
#else
int event_reporter_http_post(const struct report_event_s *event,
                             unsigned int *http_status)
{
  (void)event;
  if (http_status != NULL)
    {
      *http_status = 0;
    }

  return -ENOSYS;
}
#endif
