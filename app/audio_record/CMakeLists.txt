# ##############################################################################
# apps/examples/audio_record/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements. See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.
# ##############################################################################

if(CONFIG_EXAMPLES_AUDIO_RECORD)
  nuttx_add_application(
    MODULE
    ${CONFIG_EXAMPLES_AUDIO_RECORD}
    NAME
    ${CONFIG_EXAMPLES_AUDIO_RECORD_PROGNAME}
    SRCS
    audio_record_main.c
    STACKSIZE
    ${CONFIG_EXAMPLES_AUDIO_RECORD_STACKSIZE}
    PRIORITY
    ${CONFIG_EXAMPLES_AUDIO_RECORD_PRIORITY})
endif()
