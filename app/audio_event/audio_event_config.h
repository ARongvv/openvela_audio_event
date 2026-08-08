/*
 * Shared audio event model contract.
 *
 * The class layout is chosen at build time by the
 * EXAMPLES_AUDIO_EVENT_MODEL choice in Kconfig:
 *
 *   4-class (default): knock, cough, background, silence
 *   8-class:           knock, cough, glass_breaking, yes, no, stop,
 *                      background, silence
 */

#ifndef __APPS_EXAMPLES_AUDIO_EVENT_AUDIO_EVENT_CONFIG_H
#define __APPS_EXAMPLES_AUDIO_EVENT_AUDIO_EVENT_CONFIG_H

#define AUDIO_EVENT_SAMPLE_RATE          16000
#define AUDIO_EVENT_CLIP_SAMPLES         16000
#define AUDIO_EVENT_WINDOW_SAMPLES       480
#define AUDIO_EVENT_WINDOW_STEP_SAMPLES  320
#define AUDIO_EVENT_FFT_SIZE              512
#define AUDIO_EVENT_FEATURE_FRAMES         49
#define AUDIO_EVENT_FEATURE_BINS           40
#define AUDIO_EVENT_FEATURE_CHANNELS        3
#define AUDIO_EVENT_FEATURE_PLANE_SIZE   1960
#define AUDIO_EVENT_FEATURE_SIZE         5880

/*
 * Class layout and count come from Kconfig.
 *
 *   AUDIO_EVENT_CLASS_COUNT        total output classes
 *   AUDIO_EVENT_EVENT_CLASS_COUNT  leading event classes (knock/cough/...);
 *                                  the classes after them are keyword classes
 *                                  (yes/no/stop)
 */

#if defined(CONFIG_EXAMPLES_AUDIO_EVENT_MODEL_8CLASS) || \
    defined(CONFIG_EXAMPLES_TFLM_BENCHMARK_S3_LARGE_8CLASS)
#  define AUDIO_EVENT_CLASS_COUNT           8
#  define AUDIO_EVENT_EVENT_CLASS_COUNT     3
#else
#  define AUDIO_EVENT_CLASS_COUNT           4
#  define AUDIO_EVENT_EVENT_CLASS_COUNT     2
#endif

enum audio_event_class_e
{
  AUDIO_EVENT_CLASS_KNOCK = 0,
  AUDIO_EVENT_CLASS_COUGH,
#if defined(CONFIG_EXAMPLES_AUDIO_EVENT_MODEL_8CLASS)
  AUDIO_EVENT_CLASS_GLASS_BREAKING,
  AUDIO_EVENT_CLASS_YES,
  AUDIO_EVENT_CLASS_NO,
  AUDIO_EVENT_CLASS_STOP,
#endif
  AUDIO_EVENT_CLASS_BACKGROUND,
  AUDIO_EVENT_CLASS_SILENCE
};

#endif /* __APPS_EXAMPLES_AUDIO_EVENT_AUDIO_EVENT_CONFIG_H */
