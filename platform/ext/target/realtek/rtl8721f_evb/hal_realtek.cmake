#-------------------------------------------------------------------------------
# Copyright (c) 2025, Realtek Semiconductor Corp.
#
# SPDX-License-Identifier: BSD-3-Clause
#
#-------------------------------------------------------------------------------

cmake_policy(SET CMP0076 NEW)
set(CMAKE_CURRENT_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})

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

set(TARGET_SOC "amebag2"  CACHE STRING "")

set(HAL_REALTEK_SOC_DIR          ${HAL_REALTEK_PATH}/ameba/${TARGET_SOC}/source CACHE PATH   "")
set(HAL_REALTEK_PRJ_DIR          ${HAL_REALTEK_PATH}/ameba/${TARGET_SOC} CACHE PATH   "")
set(HAL_REALTEK_COMMON_DIR       ${HAL_REALTEK_PATH}/ameba/common CACHE PATH   "")
set(HAL_REALTEK_USRCFG_DIR       ${HAL_REALTEK_PATH}/ameba/usrcfg/${TARGET_SOC} CACHE PATH   "")
