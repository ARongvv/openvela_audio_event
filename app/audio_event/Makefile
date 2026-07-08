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

# Audio event detection built-in application info

PROGNAME  = $(CONFIG_EXAMPLES_AUDIO_EVENT_PROGNAME)
PRIORITY  = $(CONFIG_EXAMPLES_AUDIO_EVENT_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_AUDIO_EVENT_STACKSIZE)
MODULE    = $(CONFIG_EXAMPLES_AUDIO_EVENT)

# Include paths (for audio/xxx.h dsp/xxx.h etc.)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/examples/audio_event

# Source files by layer:
#   main/   - application entry
#   audio/  - audio capture
#   dsp/    - log-mel feature extraction
#   model/  - TFLite inference
#   alert/  - alert outputs

CSRCS  = main/audio_event_main.c
CSRCS += audio/audio_file.c
CSRCS += audio/audio_capture.c
CSRCS += dsp/feature_extract.c
CSRCS += detector/event_detector.c
CSRCS += alert/event_alert.c

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
CXXEXT    = .cc

TFLM_DIR = $(APPDIR)/mlearning/tflite-micro/tflite-micro

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/kissfft/kissfft
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/examples/audio_event
CXXFLAGS += ${INCDIR_PREFIX}$(TFLM_DIR)
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/gemmlowp/gemmlowp
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/ruy/ruy
CXXFLAGS += -DTFLITE_WITH_STABLE_ABI=0
CXXFLAGS += -DTFLITE_USE_OPAQUE_DELEGATE=0
CXXFLAGS += -DTFLITE_SINGLE_ROUNDING=0

ifeq ($(CONFIG_TFLITEMICRO_DEBUG),)
CXXFLAGS += -DTF_LITE_STRIP_ERROR_STRINGS
endif

include $(APPDIR)/Application.mk
