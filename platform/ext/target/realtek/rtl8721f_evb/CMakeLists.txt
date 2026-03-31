#-------------------------------------------------------------------------------
# Copyright (c) 2025, Realtek Semiconductor Corp.
#
# SPDX-License-Identifier: BSD-3-Clause
#
#-------------------------------------------------------------------------------

cmake_policy(SET CMP0076 NEW)
set(CMAKE_CURRENT_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})


# Add hal_realtek in build system
include(hal_realtek.cmake)

#========================= Platform region defs ===============================#

target_include_directories(platform_region_defs
    INTERFACE
        partition
)

target_link_libraries(platform_region_defs
    INTERFACE
        tfm_fih_headers
)

#========================= Platform common defs ===============================#

# Specify the location of platform specific build dependencies.
target_sources(tfm_s
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/cmsis_core/startup_amebag2.c
        ${CMAKE_CURRENT_SOURCE_DIR}/cmsis_core/system_core_init.c
)

# Use the common scatter file for Isolation L1 to L3
# target_add_scatter_file(tfm_s
#     $<$<C_COMPILER_ID:GNU>:${CMAKE_BINARY_DIR}/generated/platform/ext/common/gcc/tfm_isolation_s.ld>
# )

# Use the common scatter file for Isolation L1 to L2
target_add_scatter_file(tfm_s
    $<$<C_COMPILER_ID:GNU>:${CMAKE_CURRENT_SOURCE_DIR}/ld/tfm_s_ameba.ld>
)

#========================= Platform Secure ====================================#

target_include_directories(platform_s
    PUBLIC
        .
        cmsis_core
        partition
        ${HAL_REALTEK_COMMON_DIR}/include
        ${HAL_REALTEK_OS_WRAPPER_DIR}/include
        ${HAL_REALTEK_SOC_DIR}/fwlib/include
        ${HAL_REALTEK_SOC_DIR}/fwlib/include/rom
        ${HAL_REALTEK_USRCFG_DIR}/include
        ${HAL_REALTEK_USRCFG_DIR}/../common
        ${PLATFORM_DIR}/ext/target/arm/drivers/mpu/armv8m
        ${PLATFORM_DIR}/..
)

target_sources(platform_s
    INTERFACE
        $<$<STREQUAL:"${TFM_FIH_PROFILE}","HIGH">:${PLATFORM_DIR}/ext/common/template/tfm_fih_rng.c>
    PRIVATE
        cmsis_drivers/Driver_Flash.c
        ${PLATFORM_DIR}/ext/target/arm/drivers/mpu/armv8m/mpu_armv8m_drv.c
        # $<$<AND:$<NOT:$<BOOL:${TFM_PARTITION_SLIH_TEST}>>,$<NOT:$<BOOL:${TFM_PARTITION_FLIH_TEST}>>>:${CMAKE_CURRENT_SOURCE_DIR}/native_drivers/timer_cmsdk/timer_cmsdk.c>
        $<$<OR:$<BOOL:${TFM_S_REG_TEST}>,$<BOOL:${TFM_NS_REG_TEST}>>:${CMAKE_CURRENT_SOURCE_DIR}/plat_test.c>
        $<$<BOOL:${TFM_PARTITION_PLATFORM}>:${CMAKE_CURRENT_SOURCE_DIR}/services/src/tfm_platform_system.c>
)

target_sources(tfm_sprt
    PRIVATE
        # SLIH test Partition and FLIH test Partition access the timer as ARoT Partitions.
        # Put the driver to SPRT so that both SLIH and FLIH tests can access it.
        # $<$<OR:$<BOOL:${TFM_PARTITION_SLIH_TEST}>,$<BOOL:${TFM_PARTITION_FLIH_TEST}>>:${CMAKE_CURRENT_SOURCE_DIR}/native_drivers/timer_cmsdk/timer_cmsdk.c>
)

target_compile_options(platform_s
    PUBLIC
        ${COMPILER_CMSE_FLAG}
)

target_compile_definitions(platform_s
    PUBLIC
        $<$<BOOL:${TEST_NS_FPU}>:TEST_NS_FPU>
        $<$<BOOL:${TEST_S_FPU}>:TEST_S_FPU>
        CONFIG_ARM_CORE_CM4
        CONFIG_ARM_CORE_CM4_KM4TZ
)

# Add ROM symbol linker script for Realtek AmebaG2
target_link_libraries(platform_s
    PUBLIC
        -T${HAL_REALTEK_PRJ_DIR}/ld/ameba_rom_symbol_bcut_s.ld
)

#========================= tfm_spm ============================================#

target_sources(tfm_spm
    PRIVATE
        target_cfg.c
        tfm_hal_isolation.c
        tfm_hal_platform.c
        $<$<OR:$<BOOL:${CONFIG_TFM_FLIH_API}>,$<BOOL:${CONFIG_TFM_SLIH_API}>>:${CMAKE_CURRENT_SOURCE_DIR}/tfm_interrupts.c>
)

#========================= Files for building NS side platform ================#

# Copy the platform specific config
install(FILES       ${TARGET_PLATFORM_PATH}/config.cmake
        DESTINATION ${INSTALL_PLATFORM_NS_DIR})

install(DIRECTORY   partition
                    tests
        DESTINATION ${INSTALL_PLATFORM_NS_DIR}
)

############################ bin handle ########################################
# Auto-generate disassembly file after tfm_s.axf is built
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/bin/tfm_s.asm ${CMAKE_BINARY_DIR}/bin/km4tz_image3_all.bin
    DEPENDS tfm_s
    COMMAND ${CMAKE_NM} ${CMAKE_BINARY_DIR}/bin/tfm_s.axf | sort > ${CMAKE_BINARY_DIR}/bin/tfm_s.raw.map
    COMMAND ${CMAKE_OBJDUMP} -d ${CMAKE_BINARY_DIR}/bin/tfm_s.axf > ${CMAKE_BINARY_DIR}/bin/tfm_s.asm
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin/tfm_s.axf ${CMAKE_BINARY_DIR}/bin/target_pure_tfm.axf
    COMMAND ${CMAKE_STRIP} ${CMAKE_BINARY_DIR}/bin/target_pure_tfm.axf
    COMMAND ${CMAKE_OBJCOPY} -j .TFM_VECTORS
            -Obinary ${CMAKE_BINARY_DIR}/bin/target_pure_tfm.axf ${CMAKE_BINARY_DIR}/bin/ram_3_s.bin
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin/tfm_s.bin ${CMAKE_BINARY_DIR}/bin/xip_3_s.bin
    COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py pad
            -i ${CMAKE_BINARY_DIR}/bin/ram_3_s.bin -l 32
    COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py pad
            -i ${CMAKE_BINARY_DIR}/bin/xip_3_s.bin -l 32

    COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py prepend_header
            -o ${CMAKE_BINARY_DIR}/bin/ram_3_s_prepend.bin
            -i ${CMAKE_BINARY_DIR}/bin/ram_3_s.bin
            -s __vectors_start__ -m ${CMAKE_BINARY_DIR}/bin/tfm_s.raw.map
    COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py prepend_header
            -o ${CMAKE_BINARY_DIR}/bin/xip_3_s_prepend.bin
            -i ${CMAKE_BINARY_DIR}/bin/xip_3_s.bin
            -s Image$$PT_RO_START$$Base -m ${CMAKE_BINARY_DIR}/bin/tfm_s.raw.map

    COMMAND ${CMAKE_COMMAND} -E cat ${CMAKE_BINARY_DIR}/bin/xip_3_s_prepend.bin ${CMAKE_BINARY_DIR}/bin/ram_3_s_prepend.bin
            > ${CMAKE_BINARY_DIR}/bin/km4tz_image3_all.bin
    COMMENT "Generating tfm_s.asm disassembly file"
    # VERBATIM ensures that '$$' in symbol names are passed literally to the command without shell escaping
    VERBATIM
)

add_custom_target(tfm_s_asm ALL
    DEPENDS ${CMAKE_BINARY_DIR}/bin/tfm_s.asm
)
