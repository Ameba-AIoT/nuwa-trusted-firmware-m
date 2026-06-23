#-------------------------------------------------------------------------------
# Copyright (c) 2026, Realtek Semiconductor Corp.
#
# SPDX-License-Identifier: BSD-3-Clause
#
#-------------------------------------------------------------------------------

# This file is exported to NS side during CMake installation phase and renamed
# to CMakeLists.txt. It instructs how to build a platform on non-secure side.
# The structure and sources list are fully platform specific.
set(CMAKE_CURRENT_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../trusted-firmware-m/cmake)
include(remote_library)

add_library(platform_ns)

# Add hal_realtek in build system
set(TARGET_SOC "amebadplus" CACHE STRING "The target Realtek SOC (e.g., amebag2, amebadplus)")
include(hal_realtek.cmake)

#========================= Platform common defs ===============================#

# Specify the location of platform specific build dependencies.
target_sources(platform_ns
    PRIVATE
        cmsis_core/startup_amebadplus.c
        cmsis_core/system_core_init.c
        tfm_hal_platform.c
)

# Include region_defs.h and flash_layout.h
target_include_directories(platform_region_defs
    INTERFACE
        partition
)

#========================= Platform Secure ====================================#

target_include_directories(platform_ns
    PUBLIC
        ${HAL_REALTEK_COMMON_DIR}/include
        ${HAL_REALTEK_OS_WRAPPER_DIR}/include
        ${HAL_REALTEK_SOC_DIR}/fwlib/include
        ${HAL_REALTEK_SOC_DIR}/swlib
        ${HAL_REALTEK_SOC_DIR}/app/monitor/include/
        ${HAL_REALTEK_SOC_DIR}/misc
        # ${HAL_REALTEK_SOC_DIR}/lib/include
        ${HAL_REALTEK_USRCFG_DIR}/include
        ${HAL_REALTEK_USRCFG_DIR}/../common
        .
        include
        ext/cmsis/Include
        ext/cmsis/Include/m-profile
        cmsis_core
        ext/common
)

# target_sources(platform_ns
#     INTERFACE
#         $<$<STREQUAL:"${TFM_FIH_PROFILE}","HIGH">:${PLATFORM_DIR}/ext/common/template/tfm_fih_rng.c>
#     PRIVATE
#         cmsis_drivers/Driver_Flash.c
#         ${PLATFORM_DIR}/ext/target/arm/drivers/mpu/armv8m/mpu_armv8m_drv.c
#         # $<$<AND:$<NOT:$<BOOL:${TFM_PARTITION_SLIH_TEST}>>,$<NOT:$<BOOL:${TFM_PARTITION_FLIH_TEST}>>>:${CMAKE_CURRENT_SOURCE_DIR}/native_drivers/timer_cmsdk/timer_cmsdk.c>
#         # $<$<OR:$<BOOL:${TFM_S_REG_TEST}>,$<BOOL:${TFM_NS_REG_TEST}>>:${CMAKE_CURRENT_SOURCE_DIR}/plat_test.c>
#         $<$<BOOL:${TFM_PARTITION_PLATFORM}>:${CMAKE_CURRENT_SOURCE_DIR}/services/src/tfm_platform_system.c>
# )

# target_sources(tfm_sprt
#     PRIVATE
#         # SLIH test Partition and FLIH test Partition access the timer as ARoT Partitions.
#         # Put the driver to SPRT so that both SLIH and FLIH tests can access it.
#         # $<$<OR:$<BOOL:${TFM_PARTITION_SLIH_TEST}>,$<BOOL:${TFM_PARTITION_FLIH_TEST}>>:${CMAKE_CURRENT_SOURCE_DIR}/native_drivers/timer_cmsdk/timer_cmsdk.c>
# )

target_compile_options(platform_ns
    PUBLIC
        ${COMPILER_CMSE_FLAG}
)

target_compile_definitions(platform_ns
    PUBLIC
        $<$<BOOL:${TEST_NS_FPU}>:TEST_NS_FPU>
        $<$<BOOL:${TEST_S_FPU}>:TEST_S_FPU>
        CONFIG_ARM_CORE_CM4
        CONFIG_ARM_CORE_CM4_KM4TZ
)

target_compile_definitions(platform_region_defs
    INTERFACE
        $<$<BOOL:${TFM_NS_REG_TEST}>:TFM_NS_REG_TEST=1>
)

# Add ROM symbol linker script for Realtek Amebadplus
target_link_libraries(platform_ns
    PUBLIC
        -T${HAL_REALTEK_PRJ_DIR}/ld/ameba_rom_symbol_acut.ld
)


############################ bin handle ########################################
# Auto-generate disassembly and map files
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/bin/tfm_ns.asm ${CMAKE_BINARY_DIR}/bin/km4_image2_all.bin
    DEPENDS tfm_ns

    # 1. Generate map and asm
    COMMAND ${CMAKE_NM} ${CMAKE_BINARY_DIR}/bin/tfm_ns.axf | sort > ${CMAKE_BINARY_DIR}/bin/tfm_ns.raw.map
    COMMAND ${CMAKE_OBJDUMP} -d ${CMAKE_BINARY_DIR}/bin/tfm_ns.axf > ${CMAKE_BINARY_DIR}/bin/tfm_ns.asm

    # 2. Copy and strip, keep the same file names as merge_bin.py
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin/tfm_ns.axf ${CMAKE_BINARY_DIR}/bin/target_pure_img2.axf
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin/tfm_ns.raw.map ${CMAKE_BINARY_DIR}/bin/target_img2.map
    COMMAND ${CMAKE_STRIP} ${CMAKE_BINARY_DIR}/bin/target_pure_img2.axf

    # 3. Prepare xip_image2.bin
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin/tfm_ns.bin ${CMAKE_BINARY_DIR}/bin/xip_image2.bin

    # 4. Extract sections exactly like handle_amebadplus()
    COMMAND ${CMAKE_OBJCOPY} -j .tfm_ns_entry -Obinary
            ${CMAKE_BINARY_DIR}/bin/target_pure_img2.axf ${CMAKE_BINARY_DIR}/bin/entry.bin
    COMMAND ${CMAKE_OBJCOPY} -j .data -Obinary
            ${CMAKE_BINARY_DIR}/bin/target_pure_img2.axf ${CMAKE_BINARY_DIR}/bin/sram_2.bin
    COMMAND ${CMAKE_OBJCOPY} -j .null.empty -Obinary
            ${CMAKE_BINARY_DIR}/bin/target_pure_img2.axf ${CMAKE_BINARY_DIR}/bin/psram_2.bin

    # 5. Pad xip image only, same as merge_bin.py
    COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py pad
            -i ${CMAKE_BINARY_DIR}/bin/xip_image2.bin -l 32

    # 6. Prepend headers with the same symbols used by merge_bin.py
    COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py prepend_header
            -o ${CMAKE_BINARY_DIR}/bin/entry_prepend.bin
            -i ${CMAKE_BINARY_DIR}/bin/entry.bin
            -s __image2_entry_func_entry__ -m ${CMAKE_BINARY_DIR}/bin/target_img2.map

    COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py prepend_header
            -o ${CMAKE_BINARY_DIR}/bin/sram_2_prepend.bin
            -i ${CMAKE_BINARY_DIR}/bin/sram_2.bin
            -s __data_start__ -m ${CMAKE_BINARY_DIR}/bin/target_img2.map

    COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py prepend_header
            -o ${CMAKE_BINARY_DIR}/bin/psram_2_prepend.bin
            -i ${CMAKE_BINARY_DIR}/bin/psram_2.bin
            -s Image$$NS_CODE_START$$Base -m ${CMAKE_BINARY_DIR}/bin/target_img2.map

    COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py prepend_header
            -o ${CMAKE_BINARY_DIR}/bin/xip_image2_prepend.bin
            -i ${CMAKE_BINARY_DIR}/bin/xip_image2.bin
            -s Image$$NS_CODE_START$$Base -m ${CMAKE_BINARY_DIR}/bin/target_img2.map

    # 7. Concat in the exact same order as handle_amebadplus()
    COMMAND ${CMAKE_COMMAND} -E cat
            ${CMAKE_BINARY_DIR}/bin/xip_image2_prepend.bin
            ${CMAKE_BINARY_DIR}/bin/sram_2_prepend.bin
            ${CMAKE_BINARY_DIR}/bin/psram_2_prepend.bin
            ${CMAKE_BINARY_DIR}/bin/entry_prepend.bin
            > ${CMAKE_BINARY_DIR}/bin/km4_image2_all.bin

    COMMENT "Generating tfm_ns.asm and AmebaDplus-compatible km4_image2_all.bin"
    VERBATIM
)

add_custom_target(tfm_ns_asm ALL
    DEPENDS ${CMAKE_BINARY_DIR}/bin/tfm_ns.asm
)
