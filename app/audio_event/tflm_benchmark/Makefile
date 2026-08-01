############################################################################
# apps/examples/audio_event/tflm_benchmark/Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_EXAMPLES_TFLM_BENCHMARK_PROGNAME)
PRIORITY  = $(CONFIG_EXAMPLES_TFLM_BENCHMARK_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_TFLM_BENCHMARK_STACKSIZE)
MODULE    = $(CONFIG_EXAMPLES_TFLM_BENCHMARK)

CSRCS  = main/tflm_benchmark_main.c
CXXSRCS  = ../model/event_classifier.cc
CXXSRCS += ../model/model.cc
CXXEXT   = .cc

TFLM_DIR = $(APPDIR)/mlearning/tflite-micro/tflite-micro

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/examples/audio_event
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/examples/audio_event
CXXFLAGS += ${INCDIR_PREFIX}$(TFLM_DIR)
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/gemmlowp/gemmlowp
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/kissfft/kissfft
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/ruy/ruy
CXXFLAGS += -DEVENT_CLASSIFIER_ARENA_SIZE=$(CONFIG_EXAMPLES_TFLM_BENCHMARK_ARENA_SIZE)
CXXFLAGS += -DTFLITE_WITH_STABLE_ABI=0
CXXFLAGS += -DTFLITE_USE_OPAQUE_DELEGATE=0
CXXFLAGS += -DTFLITE_SINGLE_ROUNDING=0

ifeq ($(CONFIG_TFLITEMICRO_DEBUG),)
CXXFLAGS += -DTF_LITE_STRIP_ERROR_STRINGS
endif

include $(APPDIR)/Application.mk
