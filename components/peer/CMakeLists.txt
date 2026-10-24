# ESP-IDF 6 moved cJSON out of the built-in json component.
set(PEER_JSON_REQUIREMENT json)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
  set(PEER_JSON_REQUIREMENT "")
endif()
set(PEER_PROJECT_PATH "../../deps/libpeer")
file(GLOB CODES "${PEER_PROJECT_PATH}/src/*.c")

# Keep upstream submodules untouched; compile tested local transport fixes.
list(FILTER CODES EXCLUDE REGEX "/(buffer|rtp|peer_connection|dtls_srtp|sctp|agent|stun)\\.c$")
list(APPEND CODES "overrides/buffer.c" "overrides/rtp.c"
  "overrides/peer_connection.c" "overrides/dtls_srtp.c" "overrides/sctp.c" "overrides/agent.c" "overrides/stun.c")

idf_component_register(
  SRCS ${CODES}
  INCLUDE_DIRS "${PEER_PROJECT_PATH}/src" "overrides"
  REQUIRES mbedtls srtp ${PEER_JSON_REQUIREMENT} esp_netif
)

# Disable KeepAlives
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libpeer/src/config.h INPUT_CONTENT)
string(REPLACE "#define KEEPALIVE_CONNCHECK 10000" "#define KEEPALIVE_CONNCHECK 0" MODIFIED_CONTENT ${INPUT_CONTENT})
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libpeer/src/config.h ${MODIFIED_CONTENT})

if(NOT IDF_TARGET STREQUAL linux)
  add_definitions("-DESP32 -DCONFIG_USE_LWIP=1 -DCONFIG_AUDIO_BUFFER_SIZE=8096 -DCONFIG_DATA_BUFFER_SIZE=102400 -D__BYTE_ORDER=__LITTLE_ENDIAN")
endif()

add_definitions("-DHTTP_DO_NOT_USE_CUSTOM_CONFIG -DMQTT_DO_NOT_USE_CUSTOM_CONFIG -DCONFIG_USE_USRSCTP=0 -DDISABLE_PEER_SIGNALING=0")
