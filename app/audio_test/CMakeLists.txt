# ##############################################################################
# apps/examples/audio_test/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements. See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.
# ##############################################################################

if(CONFIG_EXAMPLES_AUDIO_TEST)
  nuttx_add_application(
    MODULE
    ${CONFIG_EXAMPLES_AUDIO_TEST}
    NAME
    ${CONFIG_EXAMPLES_AUDIO_TEST_PROGNAME}
    SRCS
    audio_test_main.c
    STACKSIZE
    ${CONFIG_EXAMPLES_AUDIO_TEST_STACKSIZE}
    PRIORITY
    ${CONFIG_EXAMPLES_AUDIO_TEST_PRIORITY})
endif()
