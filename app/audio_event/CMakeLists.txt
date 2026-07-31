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
      model/model.cc
      alert/event_alert.c
      reporter/event_reporter.c
      reporter/event_reporter_http.c)

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
