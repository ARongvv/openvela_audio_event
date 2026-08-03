############################################################################
# apps/examples/audio_event/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(APPDIR)/Make.defs

# Shared include paths for the audio_event application and the standalone
# tflm_benchmark variant.
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/examples/audio_event

TFLM_DIR = $(APPDIR)/mlearning/tflite-micro/tflite-micro

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/kissfft/kissfft
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/examples/audio_event
CXXFLAGS += ${INCDIR_PREFIX}$(TFLM_DIR)
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/gemmlowp/gemmlowp
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/ruy/ruy
CXXFLAGS += -DTFLITE_WITH_STABLE_ABI=0
CXXFLAGS += -DTFLITE_USE_OPAQUE_DELEGATE=0
CXXFLAGS += -DTFLITE_SINGLE_ROUNDING=0

ifneq ($(CONFIG_EXAMPLES_AUDIO_EVENT),)

PROGNAME  = $(CONFIG_EXAMPLES_AUDIO_EVENT_PROGNAME)
PRIORITY  = $(CONFIG_EXAMPLES_AUDIO_EVENT_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_AUDIO_EVENT_STACKSIZE)
MODULE    = $(CONFIG_EXAMPLES_AUDIO_EVENT)

# Source files by layer:
#   main/   - application entry
#   audio/  - audio capture
#   dsp/    - log-mel feature extraction
#   model/  - TFLite inference
#   alert/  - alert outputs

CSRCS  = main/audio_event_main.c
CSRCS += audio/audio_file.c
CSRCS += audio/audio_capture.c
CSRCS += power/power_gate.c
CSRCS += dsp/feature_extract.c
CSRCS += detector/event_detector.c
CSRCS += alert/event_alert.c
CSRCS += reporter/event_reporter.c
CSRCS += reporter/event_reporter_http.c

ifeq ($(CONFIG_EXAMPLES_AUDIO_EVENT_UI),y)
CSRCS += ui/audio_event_ui.c
CSRCS += ui/ui_status_bar.c
CSRCS += ui/ui_waveform.c
CSRCS += ui/ui_probability.c
CSRCS += ui/ui_detection.c
CSRCS += ui/ui_params.c
CSRCS += ui/ui_anim.c
endif

ifeq ($(CONFIG_EXAMPLES_AUDIO_EVENT_OLED_UI),y)
CSRCS += ui_oled/audio_event_oled_ui.c
endif

CXXSRCS  = model/event_classifier.cc
CXXSRCS += model/model.cc

endif

ifneq ($(CONFIG_EXAMPLES_TFLM_BENCHMARK),)

PROGNAME  = $(CONFIG_EXAMPLES_TFLM_BENCHMARK_PROGNAME)
PRIORITY  = $(CONFIG_EXAMPLES_TFLM_BENCHMARK_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_TFLM_BENCHMARK_STACKSIZE)
MODULE    = $(CONFIG_EXAMPLES_TFLM_BENCHMARK)

CSRCS  = tflm_benchmark/main/tflm_benchmark_main.c
ifeq ($(CONFIG_EXAMPLES_TFLM_BENCHMARK_BCRESNET1_8CLASS_PREFLIGHT),y)
CXXSRCS  = model/bcresnet1_8class_preflight.cc
CXXSRCS += model/bcresnet1_8class_model.cc
else ifeq ($(CONFIG_EXAMPLES_TFLM_BENCHMARK_S3_LARGE_8CLASS),y)
CXXSRCS  = model/event_classifier.cc
CXXSRCS += model/s3_large_8class_model.cc
else
CXXSRCS  = model/event_classifier.cc
CXXSRCS += model/model.cc
endif
CXXFLAGS += -DEVENT_CLASSIFIER_ARENA_SIZE=$(CONFIG_EXAMPLES_TFLM_BENCHMARK_ARENA_SIZE)

endif

CXXEXT = .cc

ifeq ($(CONFIG_TFLITEMICRO_DEBUG),)
CXXFLAGS += -DTF_LITE_STRIP_ERROR_STRINGS
endif

include $(APPDIR)/Application.mk
