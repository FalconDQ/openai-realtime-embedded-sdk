cmake_minimum_required(VERSION 3.19)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/local_config.cmake")

if(DEFINED ENV{LOG_DATACHANNEL_MESSAGES})
  add_compile_definitions(LOG_DATACHANNEL_MESSAGES="1")
endif()


add_compile_definitions(OPENAI_REALTIMEAPI="https://api.openai.com/v1/realtime/calls")

set(COMPONENTS src)
set(EXTRA_COMPONENT_DIRS "src" "components/srtp" "components/peer" "components/esp-libopus")

if(IDF_TARGET STREQUAL linux)
	add_compile_definitions(LINUX_BUILD=1)
  list(APPEND EXTRA_COMPONENT_DIRS
    $ENV{IDF_PATH}/examples/protocols/linux_stubs/esp_stubs
    "components/esp-protocols/common_components/linux_compat/esp_timer"
    "components/esp-protocols/common_components/linux_compat/freertos"
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(src)
