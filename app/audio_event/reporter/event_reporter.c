/*
 * Fixed-size queue and worker for asynchronous remote event reporting.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "audio_event_config.h"
#include "reporter/event_reporter.h"

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_REPORT
struct event_reporter_s
{
  pthread_mutex_t lock;
  sem_t wake_sem;
  pthread_t thread;
  struct report_event_s queue[CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_QUEUE_DEPTH];
  struct event_reporter_stats_s stats;
  uint32_t next_sequence;
  uint32_t boot_id;
  unsigned int head;
  unsigned int count;
  bool initialized;
  bool stopping;
  bool thread_created;
};

static struct event_reporter_s g_reporter;

static void reporter_sanitize_name(char *destination, size_t destination_size,
                                   const char *source)
{
  size_t index;

  if (destination_size == 0)
    {
      return;
    }

  if (source == NULL)
    {
      source = "unknown";
    }

  for (index = 0; index + 1 < destination_size && source[index] != '\0';
       index++)
    {
      char value = source[index];

      if ((value >= 'a' && value <= 'z') ||
          (value >= 'A' && value <= 'Z') ||
          (value >= '0' && value <= '9') || value == '_' || value == '-')
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

static bool reporter_is_stopping(void)
{
  bool stopping;

  pthread_mutex_lock(&g_reporter.lock);
  stopping = g_reporter.stopping;
  pthread_mutex_unlock(&g_reporter.lock);
  return stopping;
}

static void reporter_sleep_ms(uint32_t milliseconds)
{
  struct timespec request;

  request.tv_sec = milliseconds / 1000;
  request.tv_nsec = (long)(milliseconds % 1000) * 1000000L;

  while (nanosleep(&request, &request) < 0 && errno == EINTR)
    {
    }
}

static uint32_t reporter_backoff_ms(unsigned int retry)
{
  uint64_t delay = CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_RETRY_INITIAL_MS;

  while (retry > 0 &&
         delay < CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_RETRY_MAX_DELAY_MS)
    {
      delay <<= 1;
      retry--;
    }

  if (delay > CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_RETRY_MAX_DELAY_MS)
    {
      delay = CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_RETRY_MAX_DELAY_MS;
    }

  return (uint32_t)delay;
}

static bool reporter_peek(struct report_event_s *event)
{
  bool has_event = false;

  pthread_mutex_lock(&g_reporter.lock);
  if (g_reporter.count > 0)
    {
      *event = g_reporter.queue[g_reporter.head];
      has_event = true;
    }

  pthread_mutex_unlock(&g_reporter.lock);
  return has_event;
}

static void reporter_complete(bool sent, int error, unsigned int http_status)
{
  pthread_mutex_lock(&g_reporter.lock);

  if (g_reporter.count > 0)
    {
      g_reporter.head = (g_reporter.head + 1) %
                        CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_QUEUE_DEPTH;
      g_reporter.count--;
      g_reporter.stats.queued_events = g_reporter.count;
    }

  g_reporter.stats.last_error = error;
  g_reporter.stats.last_http_status = http_status;
  if (sent)
    {
      g_reporter.stats.sent_events++;
    }
  else
    {
      g_reporter.stats.dropped_events++;
    }

  pthread_mutex_unlock(&g_reporter.lock);
}

static void reporter_note_failure(int error, unsigned int http_status)
{
  pthread_mutex_lock(&g_reporter.lock);
  g_reporter.stats.failed_attempts++;
  g_reporter.stats.last_error = error;
  g_reporter.stats.last_http_status = http_status;
  pthread_mutex_unlock(&g_reporter.lock);
}

static void reporter_note_retry(void)
{
  pthread_mutex_lock(&g_reporter.lock);
  g_reporter.stats.retries++;
  pthread_mutex_unlock(&g_reporter.lock);
}

static void reporter_send_event(const struct report_event_s *event)
{
  unsigned int retry = 0;

  for (;;)
    {
      unsigned int http_status = 0;
      int ret = event_reporter_http_post(event, &http_status);

      if (ret == 0)
        {
          reporter_complete(true, 0, http_status);
          return;
        }

      reporter_note_failure(ret, http_status);
      if (retry >= CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_RETRY_MAX ||
          reporter_is_stopping())
        {
          reporter_complete(false, ret, http_status);
          return;
        }

      reporter_note_retry();
      reporter_sleep_ms(reporter_backoff_ms(retry));
      retry++;
    }
}

static void *reporter_thread_main(void *argument)
{
  struct report_event_s event;

  (void)argument;

  for (;;)
    {
      while (sem_wait(&g_reporter.wake_sem) < 0)
        {
          if (errno != EINTR)
            {
              return NULL;
            }
        }

      if (reporter_is_stopping())
        {
          return NULL;
        }

      while (reporter_peek(&event))
        {
          reporter_send_event(&event);
          if (reporter_is_stopping())
            {
              return NULL;
            }
        }
    }
}

static int reporter_start_thread(void)
{
  pthread_attr_t attr;
  struct sched_param param;
  int ret;

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_attr_setstacksize(
      &attr, CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_THREAD_STACKSIZE);
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
      param.sched_priority = CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_THREAD_PRIORITY;
      ret = pthread_attr_setschedparam(&attr, &param);
    }

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_SMP_AFFINITY
  if (ret == 0)
    {
      cpu_set_t cpuset;

      CPU_ZERO(&cpuset);
      CPU_SET(1, &cpuset);
      ret = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
    }
#endif

  if (ret == 0)
    {
      ret = pthread_create(&g_reporter.thread, &attr, reporter_thread_main,
                           NULL);
    }

  pthread_attr_destroy(&attr);
  return ret == 0 ? 0 : -ret;
}
#endif

int event_reporter_init(void)
{
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_REPORT
  struct timespec time_value;
  int ret;

  memset(&g_reporter, 0, sizeof(g_reporter));
  ret = pthread_mutex_init(&g_reporter.lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  ret = sem_init(&g_reporter.wake_sem, 0, 0);
  if (ret != 0)
    {
      pthread_mutex_destroy(&g_reporter.lock);
      return -errno;
    }

  if (clock_gettime(CLOCK_MONOTONIC, &time_value) == 0)
    {
      g_reporter.boot_id = (uint32_t)(time_value.tv_sec * 1000 +
                                      time_value.tv_nsec / 1000000);
    }

  g_reporter.stats.enabled = true;
  ret = reporter_start_thread();
  if (ret < 0)
    {
      sem_destroy(&g_reporter.wake_sem);
      pthread_mutex_destroy(&g_reporter.lock);
      return ret;
    }

  g_reporter.thread_created = true;
  g_reporter.initialized = true;
  printf("[reporter] http enabled queue=%d endpoint=%s\n",
         CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_QUEUE_DEPTH,
         CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_HTTP_ENDPOINT[0] == '\0' ?
         "<unset>" : "<configured>");
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_SMP_AFFINITY
  printf("[reporter] affinity=CPU1 priority=%d\n",
         CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_THREAD_PRIORITY);
#endif
  return 0;
#else
  return 0;
#endif
}

void event_reporter_deinit(void)
{
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_REPORT
  if (!g_reporter.initialized)
    {
      return;
    }

  pthread_mutex_lock(&g_reporter.lock);
  g_reporter.stopping = true;
  pthread_mutex_unlock(&g_reporter.lock);
  sem_post(&g_reporter.wake_sem);

  if (g_reporter.thread_created)
    {
      pthread_join(g_reporter.thread, NULL);
      g_reporter.thread_created = false;
    }

  sem_destroy(&g_reporter.wake_sem);
  pthread_mutex_destroy(&g_reporter.lock);
  g_reporter.initialized = false;
#endif
}

int event_reporter_enqueue(int event_class_id, const char *event_name,
                           uint16_t confidence_permille,
                           uint64_t monotonic_ms)
{
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_REPORT
  struct report_event_s *event;
  bool wake_worker;
  unsigned int tail;

  if (!g_reporter.initialized)
    {
      return -EAGAIN;
    }

  pthread_mutex_lock(&g_reporter.lock);
  if (g_reporter.stopping)
    {
      pthread_mutex_unlock(&g_reporter.lock);
      return -ECANCELED;
    }

  if (g_reporter.count == CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_QUEUE_DEPTH)
    {
      g_reporter.stats.dropped_events++;
      pthread_mutex_unlock(&g_reporter.lock);
      return -ENOSPC;
    }

  wake_worker = g_reporter.count == 0;
  tail = (g_reporter.head + g_reporter.count) %
         CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_QUEUE_DEPTH;
  event = &g_reporter.queue[tail];
  memset(event, 0, sizeof(*event));
  event->sequence = ++g_reporter.next_sequence;
  event->boot_id = g_reporter.boot_id;
  event->monotonic_ms = monotonic_ms;
  event->event_class_id = event_class_id;
  event->confidence_permille = confidence_permille;
  reporter_sanitize_name(event->event_name, sizeof(event->event_name),
                         event_name);
  g_reporter.count++;
  g_reporter.stats.queued_events = g_reporter.count;
  if (g_reporter.count > g_reporter.stats.queue_high_water)
    {
      g_reporter.stats.queue_high_water = g_reporter.count;
    }

  pthread_mutex_unlock(&g_reporter.lock);

  if (wake_worker)
    {
      sem_post(&g_reporter.wake_sem);
    }

  return 0;
#else
  (void)event_class_id;
  (void)event_name;
  (void)confidence_permille;
  (void)monotonic_ms;
  return 0;
#endif
}

void event_reporter_get_stats(struct event_reporter_stats_s *stats)
{
  if (stats == NULL)
    {
      return;
    }

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_REMOTE_REPORT
  if (g_reporter.initialized)
    {
      pthread_mutex_lock(&g_reporter.lock);
      *stats = g_reporter.stats;
      pthread_mutex_unlock(&g_reporter.lock);
      return;
    }
#endif

  memset(stats, 0, sizeof(*stats));
}
