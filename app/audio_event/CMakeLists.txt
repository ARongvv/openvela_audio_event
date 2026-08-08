# ##############################################################################
# apps/examples/audio_event/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements. See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.
# ##############################################################################

if(CONFIG_EXAMPLES_AUDIO_EVENT)
  set(AUDIO_EVENT_SRCS
      main/audio_event_main.c
      audio/audio_file.c
      audio/audio_capture.c
      power/power_gate.c
      dsp/feature_extract.c
      detector/event_detector.c
      model/event_classifier.cc
      alert/event_alert.c
      reporter/event_reporter.c
      reporter/event_reporter_http.c)

  if(CONFIG_EXAMPLES_AUDIO_EVENT_MODEL_8CLASS)
    list(APPEND AUDIO_EVENT_SRCS model/s3_large_8class_model.cc)
  else()
    list(APPEND AUDIO_EVENT_SRCS model/model.cc)
  endif()

  if(CONFIG_EXAMPLES_AUDIO_EVENT_UI)
    list(APPEND AUDIO_EVENT_SRCS
         ui/audio_event_ui.c
         ui/ui_status_bar.c
         ui/ui_waveform.c
         ui/ui_probability.c
         ui/ui_detection.c
         ui/ui_params.c
         ui/ui_anim.c)
  endif()

  if(CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI)
    list(APPEND AUDIO_EVENT_SRCS ui_oled/audio_event_oled_ui.c)
  endif()

  nuttx_add_application(
    NAME
    audio_event
    SRCS
    ${AUDIO_EVENT_SRCS}
    STACKSIZE
    ${CONFIG_EXAMPLES_AUDIO_EVENT_STACKSIZE}
    PRIORITY
    ${CONFIG_EXAMPLES_AUDIO_EVENT_PRIORITY}
    INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_LIST_DIR}
    DEPENDS
    tflite_micro
    kissfft
    DEFINITIONS
    TFLITE_WITH_STABLE_ABI=0
    TFLITE_USE_OPAQUE_DELEGATE=0
    TFLITE_SINGLE_ROUNDING=0)
endif()

if(CONFIG_EXAMPLES_TFLM_BENCHMARK)
  if(CONFIG_EXAMPLES_TFLM_BENCHMARK_BCRESNET1_8CLASS_PREFLIGHT)
    set(TFLM_BENCHMARK_MODEL_SRCS
        model/bcresnet1_8class_preflight.cc
        model/bcresnet1_8class_model.cc)
  elseif(CONFIG_EXAMPLES_TFLM_BENCHMARK_S3_LARGE_8CLASS)
    set(TFLM_BENCHMARK_MODEL_SRCS
        model/event_classifier.cc
        model/s3_large_8class_model.cc)
  else()
    set(TFLM_BENCHMARK_MODEL_SRCS
        model/event_classifier.cc
        model/model.cc)
  endif()

  nuttx_add_application(
    NAME
    tflm_benchmark
    SRCS
    tflm_benchmark/main/tflm_benchmark_main.c
    ${TFLM_BENCHMARK_MODEL_SRCS}
    STACKSIZE
    ${CONFIG_EXAMPLES_TFLM_BENCHMARK_STACKSIZE}
    PRIORITY
    ${CONFIG_EXAMPLES_TFLM_BENCHMARK_PRIORITY}
    INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_LIST_DIR}
    DEPENDS
    tflite_micro
    DEFINITIONS
    EVENT_CLASSIFIER_ARENA_SIZE=${CONFIG_EXAMPLES_TFLM_BENCHMARK_ARENA_SIZE}
    TFLITE_WITH_STABLE_ABI=0
    TFLITE_USE_OPAQUE_DELEGATE=0
    TFLITE_SINGLE_ROUNDING=0)
endif()
