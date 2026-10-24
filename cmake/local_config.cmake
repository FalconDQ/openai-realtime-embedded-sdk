# Run before ESP-IDF's project setup, including during `idf.py set-target`.
if(NOT DEFINED PYTHON OR "${PYTHON}" STREQUAL "")
  if(DEFINED ENV{IDF_PYTHON_ENV_PATH})
    if(WIN32)
      set(PYTHON "$ENV{IDF_PYTHON_ENV_PATH}/Scripts/python.exe")
    else()
      set(PYTHON "$ENV{IDF_PYTHON_ENV_PATH}/bin/python")
    endif()
  else()
    find_program(PYTHON NAMES python3 python REQUIRED)
  endif()
endif()

set(LOCAL_CONFIG_INCLUDE_DIR "${CMAKE_BINARY_DIR}/local_config")
set(_local_config_script "${CMAKE_CURRENT_LIST_DIR}/../tools/configure_secrets.py")
set(_local_dotenv "${CMAKE_CURRENT_LIST_DIR}/../.env")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${_local_dotenv}" "${_local_config_script}")

execute_process(
  COMMAND "${PYTHON}" "${_local_config_script}"
    --dotenv "${_local_dotenv}"
    --output "${LOCAL_CONFIG_INCLUDE_DIR}/local_secrets.h"
    --target "${IDF_TARGET}"
  RESULT_VARIABLE _local_config_result
  OUTPUT_QUIET
  ERROR_VARIABLE _local_config_error)
if(NOT "${_local_config_result}" STREQUAL "0")
  message(FATAL_ERROR "${_local_config_error}\nLocal configuration generation failed.")
endif()
