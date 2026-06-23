#-------------------------------------------------------------------------------
# Copyright (c) 2025, Realtek Semiconductor Corp.
#
# SPDX-License-Identifier: BSD-3-Clause
#
#-------------------------------------------------------------------------------

cmake_policy(SET CMP0076 NEW)

if(NOT DEFINED TARGET_SOC OR "${TARGET_SOC}" STREQUAL "")
    message(FATAL_ERROR "TARGET_SOC is missing or empty! "
                        "Please set TARGET_SOC (e.g., set(TARGET_SOC \"amebag2\")) "
                        "before including hal_realtek.cmake")
endif()

###### CMake-generic extensions ################################################
function(add_subdirectory_ifdef feature_toggle source_dir)
  if(${${feature_toggle}})
    add_subdirectory(${source_dir} ${ARGN})
  endif()
endfunction()

################################################################################
# Fetch hal_realtek repository
set(FETCHCONTENT_QUIET TRUE)

fetch_remote_library(
    LIB_NAME                hal_realtek
    LIB_SOURCE_PATH_VAR     HAL_REALTEK_PATH
    FETCH_CONTENT_ARGS
        GIT_REPOSITORY      https://github.com/zephyrproject-rtos/hal_realtek
        GIT_TAG             ${HAL_REALTEK_VERSION}
        GIT_PROGRESS        TRUE
)

set(HAL_REALTEK_PATH            ${HAL_REALTEK_PATH}  CACHE PATH "")

set(HAL_REALTEK_SOC_DIR          ${HAL_REALTEK_PATH}/ameba/${TARGET_SOC}/source CACHE PATH   "")
set(HAL_REALTEK_PRJ_DIR          ${HAL_REALTEK_PATH}/ameba/${TARGET_SOC} CACHE PATH   "")
set(HAL_REALTEK_COMMON_DIR       ${HAL_REALTEK_PATH}/ameba/common CACHE PATH   "")
set(HAL_REALTEK_OS_WRAPPER_DIR   ${HAL_REALTEK_PATH}/ameba/common/os_wrapper CACHE PATH   "")
set(HAL_REALTEK_USRCFG_DIR       ${HAL_REALTEK_PATH}/ameba/usrcfg/${TARGET_SOC} CACHE PATH   "")
set(HAL_REALTEK_LIB_DIR          ${HAL_REALTEK_PATH}/zephyr/blobs/ameba/${TARGET_SOC}/lib CACHE PATH   "")
