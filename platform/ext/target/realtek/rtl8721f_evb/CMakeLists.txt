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

set(AUTO_GEN_SECURE_PUB_C ${CMAKE_CURRENT_BINARY_DIR}/auto_gen_secure_pub_key.c)
set(AUTO_GEN_NONSECURE_PUB_C ${CMAKE_CURRENT_BINARY_DIR}/auto_gen_nonsecure_pub_key.c)
set(SECURE_SIGN_PUB_PEM ${CMAKE_BINARY_DIR}/bl2/ext/mcuboot/image_s_signing_public_key.pem)
set(NONSECURE_SIGN_PUB_PEM ${CMAKE_BINARY_DIR}/bl2/ext/mcuboot/image_ns_signing_public_key.pem)
set(SECURE_KEY_PUB_BIN secure_key_pub.bin)
set(NONSECURE_KEY_PUB_BIN nonsecure_key_pub.bin)
string(REGEX REPLACE "[.]" "_" SECURE_KEY_PUB_ARRAY_NAME "${SECURE_KEY_PUB_BIN}")
string(REGEX REPLACE "[.]" "_" NONSECURE_KEY_PUB_ARRAY_NAME "${NONSECURE_KEY_PUB_BIN}")

add_custom_command(
    OUTPUT ${AUTO_GEN_SECURE_PUB_C} ${AUTO_GEN_NONSECURE_PUB_C}
    DEPENDS ${SECURE_SIGN_PUB_PEM} ${NONSECURE_SIGN_PUB_PEM}
    COMMAND /bin/sh -c "openssl ec -in '${SECURE_SIGN_PUB_PEM}' -pubin -outform DER | tail -c 65 > '${CMAKE_CURRENT_BINARY_DIR}/${SECURE_KEY_PUB_BIN}'"
    COMMAND /bin/sh -c "cd '${CMAKE_CURRENT_BINARY_DIR}' && xxd -i ${SECURE_KEY_PUB_BIN} > '${AUTO_GEN_SECURE_PUB_C}'"

    COMMAND /bin/sh -c "openssl ec -in '${NONSECURE_SIGN_PUB_PEM}' -pubin -outform DER | tail -c 65 > '${CMAKE_CURRENT_BINARY_DIR}/${NONSECURE_KEY_PUB_BIN}'"
    COMMAND /bin/sh -c "cd '${CMAKE_CURRENT_BINARY_DIR}' && xxd -i ${NONSECURE_KEY_PUB_BIN} > '${AUTO_GEN_NONSECURE_PUB_C}'"
    COMMENT "Generate auto_gen_secure_pub_key.c"
    VERBATIM
)
add_custom_target(gen_pub_key_c ALL
    DEPENDS ${AUTO_GEN_SECURE_PUB_C} ${AUTO_GEN_NONSECURE_PUB_C}
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
        ${HAL_REALTEK_SOC_DIR}/fwlib/ram_common/ameba_flash_ram.c
        # $<$<AND:$<NOT:$<BOOL:${TFM_PARTITION_SLIH_TEST}>>,$<NOT:$<BOOL:${TFM_PARTITION_FLIH_TEST}>>>:${CMAKE_CURRENT_SOURCE_DIR}/native_drivers/timer_cmsdk/timer_cmsdk.c>
        # $<$<OR:$<BOOL:${TFM_S_REG_TEST}>,$<BOOL:${TFM_NS_REG_TEST}>>:${CMAKE_CURRENT_SOURCE_DIR}/plat_test.c>
        $<$<BOOL:${TFM_PARTITION_PLATFORM}>:${CMAKE_CURRENT_SOURCE_DIR}/services/src/tfm_platform_system.c>
        $<$<NOT:$<BOOL:${PLATFORM_DEFAULT_OTP}>>:${CMAKE_CURRENT_SOURCE_DIR}/amebag2_otp.c>
        $<$<NOT:$<BOOL:${PLATFORM_DEFAULT_OTP}>>:${AUTO_GEN_NONSECURE_PUB_C}>
        $<$<NOT:$<BOOL:${PLATFORM_DEFAULT_OTP}>>:${AUTO_GEN_SECURE_PUB_C}>
)

if(NOT PLATFORM_DEFAULT_OTP)
    add_dependencies(platform_s gen_pub_key_c)
endif()

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
        SECURE_KEY_PUB_ARRAY_NAME=${SECURE_KEY_PUB_ARRAY_NAME}
        NONSECURE_KEY_PUB_ARRAY_NAME=${NONSECURE_KEY_PUB_ARRAY_NAME}
)

target_compile_definitions(platform_region_defs
    INTERFACE
        $<$<BOOL:${TFM_S_REG_TEST}>:TFM_S_REG_TEST=1>
)

# Add ROM symbol linker script for Realtek AmebaG2
target_link_libraries(platform_s
    PUBLIC
        -T${HAL_REALTEK_PRJ_DIR}/ld/ameba_rom_symbol_bcut_s.ld
)

if (TFM_PARTITION_CRYPTO)
    target_include_directories(platform_crypto_keys
        PUBLIC
            .
    )
endif()
#========================= Platform BL2 =======================================#

if(BL2)
    target_compile_options(platform_bl2
        PUBLIC
            ${COMPILER_CMSE_FLAG}
    )

    target_compile_definitions(platform_bl2
        PRIVATE
            CONFIG_ARM_CORE_CM4
            CONFIG_ARM_CORE_CM4_KM4TZ
    )

    target_include_directories(platform_bl2
        PUBLIC
            .
            ${HAL_REALTEK_SOC_DIR}/fwlib/include
            ${HAL_REALTEK_SOC_DIR}/fwlib/include/rom
            ${HAL_REALTEK_SOC_DIR}/../include
            ${HAL_REALTEK_COMMON_DIR}/include
            ${HAL_REALTEK_USRCFG_DIR}/include
            ${HAL_REALTEK_USRCFG_DIR}/../common
            ${HAL_REALTEK_OS_WRAPPER_DIR}/include
    )

    target_compile_options(bl2
        PUBLIC
            ${COMPILER_CMSE_FLAG}
    )

    target_compile_definitions(bl2
        PUBLIC
            CONFIG_ARM_CORE_CM4
            CONFIG_ARM_CORE_CM4_KM4TZ
            BLOCK_ERASE_DEFINE
            SECURE_KEY_PUB_ARRAY_NAME=${SECURE_KEY_PUB_ARRAY_NAME}
            NONSECURE_KEY_PUB_ARRAY_NAME=${NONSECURE_KEY_PUB_ARRAY_NAME}
    )

    target_include_directories(bl2
        PUBLIC
            .
            ${HAL_REALTEK_SOC_DIR}/fwlib/include
            ${HAL_REALTEK_SOC_DIR}/loader/bootloader
            ${HAL_REALTEK_SOC_DIR}/loader/mcuboot
            # ${HAL_REALTEK_SOC_DIR}/fwlib/include/rom
            ${HAL_REALTEK_SOC_DIR}/../include
            ${HAL_REALTEK_COMMON_DIR}/include
            ${HAL_REALTEK_USRCFG_DIR}/include
            ${HAL_REALTEK_USRCFG_DIR}/../common
            ${HAL_REALTEK_OS_WRAPPER_DIR}/include
    )

    target_sources(bl2
        PRIVATE
            cmsis_drivers/Driver_Flash.c
            tfm_hal_platform.c
            ${HAL_REALTEK_SOC_DIR}/loader/mcuboot/boot_prepare.c

            ${CMAKE_CURRENT_SOURCE_DIR}/../common/startup_bl2.c
            ${CMAKE_CURRENT_SOURCE_DIR}/cmsis_core/system_core_init.c
            ${CMAKE_CURRENT_SOURCE_DIR}/amebag2_otp.c
            ${AUTO_GEN_SECURE_PUB_C}
            ${AUTO_GEN_NONSECURE_PUB_C}

            ${HAL_REALTEK_SOC_DIR}/loader/bootloader/bootloader_km4tz.c
            ${HAL_REALTEK_SOC_DIR}/loader/bootloader/bootloader_clk.c
            ${HAL_REALTEK_SOC_DIR}/loader/bootloader/boot_ota_km4tz.c
            ${HAL_REALTEK_SOC_DIR}/fwlib/ram_common/ameba_psram.c
            ${HAL_REALTEK_SOC_DIR}/fwlib/ram_common/ameba_ldo.c
            ${HAL_REALTEK_SOC_DIR}/fwlib/ram_common/ameba_pll.c
            ${HAL_REALTEK_SOC_DIR}/fwlib/ram_common/ameba_clk.c
            ${HAL_REALTEK_SOC_DIR}/fwlib/ram_common/ameba_reset.c
            ${HAL_REALTEK_SOC_DIR}/fwlib/ram_common/ameba_flash_ram.c
            ${HAL_REALTEK_SOC_DIR}/fwlib/ram_km4tz/ameba_flashclk.c

            ${HAL_REALTEK_USRCFG_DIR}/ameba_bootcfg.c
            ${HAL_REALTEK_USRCFG_DIR}/ameba_flashcfg.c

            $<$<NOT:$<BOOL:${DEFAULT_MCUBOOT_SECURITY_COUNTERS}>>:${HAL_REALTEK_SOC_DIR}/loader/mcuboot/mcuboot_security_cnt.c>
            $<$<NOT:$<BOOL:${DEFAULT_MCUBOOT_SECURITY_COUNTERS}>>:${HAL_REALTEK_SOC_DIR}/loader/mcuboot/ameba_security_counter_utils.c>
    )

    target_link_libraries(bl2
        PUBLIC
            -T${HAL_REALTEK_PRJ_DIR}/ld/ameba_rom_symbol_bcut_s.ld
            ${HAL_REALTEK_LIB_DIR}/lib_chipinfo.a
            ${HAL_REALTEK_LIB_DIR}/lib_bootloader.a
    )
    target_add_scatter_file(bl2
            $<$<C_COMPILER_ID:GNU>:${CMAKE_CURRENT_SOURCE_DIR}/ld/tfm_common_bl2.ld>
    )
    #NOTE: Apply CMSE flags when preprocessing the BL2 scatter script so that
    # __ARM_FEATURE_CMSE and relative symbols (e.g. __StackSeal) are defined.
    # These flags must be set on bl2_scatter (the linker-script preprocessor target);
    # adding them only to bl2 would not affect this preprocessing step
    target_compile_options(bl2_scatter
        PUBLIC
            ${COMPILER_CMSE_FLAG}
    )

    add_dependencies(bl2 gen_pub_key_c)
endif()

#========================= tfm_spm ============================================#

target_sources(tfm_spm
    PRIVATE
        target_cfg.c
        tfm_hal_isolation.c
        tfm_hal_platform.c
        # $<$<OR:$<BOOL:${CONFIG_TFM_FLIH_API}>,$<BOOL:${CONFIG_TFM_SLIH_API}>>:${CMAKE_CURRENT_SOURCE_DIR}/tfm_interrupts.c>
)

#========================= Files for building NS side platform ================#

install(DIRECTORY   ${TARGET_PLATFORM_PATH}/cmsis_core
                    # ${PLATFORM_DIR}/include/
        DESTINATION ${INSTALL_PLATFORM_NS_DIR})

install(DIRECTORY   ${PLATFORM_DIR}/ext/common
        DESTINATION ${INSTALL_PLATFORM_NS_DIR}/ext)

install(FILES       ${CMAKE_CURRENT_SOURCE_DIR}/tfm_hal_platform.c
        DESTINATION ${INSTALL_PLATFORM_NS_DIR})

install(FILES       ${TARGET_PLATFORM_PATH}/partition/region_defs.h
                    ${TARGET_PLATFORM_PATH}/partition/flash_layout.h
                    ${PLATFORM_DIR}/include/tfm_hal_device_header.h
                    ${TARGET_PLATFORM_PATH}/device_cfg.h
                    ${TARGET_PLATFORM_PATH}/target_cfg.h
                    ${TARGET_PLATFORM_PATH}/tfm_peripherals_def.h
                    ${PLATFORM_DIR}/../secure_fw/partitions/initial_attestation/attest.h
                    ${PLATFORM_DIR}/../secure_fw/partitions/initial_attestation/attest_token.h
                    ${PLATFORM_DIR}/../secure_fw/spm/include/boot/tfm_boot_status.h
        DESTINATION ${INSTALL_PLATFORM_NS_DIR}/include)

install(FILES       ${CMAKE_CURRENT_SOURCE_DIR}/ld/tfm_ns_ameba.ld
        DESTINATION ${INSTALL_PLATFORM_NS_DIR}/linker_scripts)

# copy all files from active platform directory
install(DIRECTORY   ${TARGET_PLATFORM_PATH}/ns/
        DESTINATION ${INSTALL_PLATFORM_NS_DIR})

install(FILES       ${TARGET_PLATFORM_PATH}/hal_realtek.cmake
        DESTINATION ${INSTALL_PLATFORM_NS_DIR})

install(FILES       ${TARGET_PLATFORM_PATH}/cpuarch.cmake
        DESTINATION ${INSTALL_PLATFORM_NS_DIR})

# Copy the platform specific config
install(FILES       ${TARGET_PLATFORM_PATH}/config.cmake
        DESTINATION ${INSTALL_PLATFORM_NS_DIR})

install(DIRECTORY   partition
                    tests
        DESTINATION ${INSTALL_PLATFORM_NS_DIR}
)

############################ bin handle ########################################

set(IMAGES_DIR  ${CMAKE_BINARY_DIR}/../images)
set(BIN_DIR     ${CMAKE_BINARY_DIR}/bin)
if(BL2)
    # Auto-generate disassembly file after bl2.axf is built
    file(MAKE_DIRECTORY ${BIN_DIR})
    file(MAKE_DIRECTORY ${IMAGES_DIR})
    if(NOT EXISTS ${BIN_DIR}/ram_1.bin)
        file(TOUCH ${BIN_DIR}/ram_1.bin)
    endif()
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/amebagreen2_gcc_project)
    file(COPY ${HAL_REALTEK_PRJ_DIR}/manifest.json5 DESTINATION ${CMAKE_BINARY_DIR}/amebagreen2_gcc_project/)
    file(COPY ${HAL_REALTEK_PRJ_DIR}/ameba_layout.ld DESTINATION ${CMAKE_BINARY_DIR}/amebagreen2_gcc_project/)
    add_custom_command(
        OUTPUT ${IMAGES_DIR}/${TARGET_SOC}_boot.bin
        BYPRODUCTS
            ${BIN_DIR}/bl2.raw.map
            ${BIN_DIR}/boot.bin
            ${BIN_DIR}/${TARGET_SOC}_boot.bin
        DEPENDS
            bl2 platform_bl2 platform_s
            ${BIN_DIR}/bl2.axf
            ${BIN_DIR}/bl2.bin
            ${BIN_DIR}/ram_1.bin
            ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py
        COMMAND ${CMAKE_NM} ${BIN_DIR}/bl2.axf | sort > ${BIN_DIR}/bl2.raw.map
        COMMAND ${CMAKE_OBJDUMP} -d ${BIN_DIR}/bl2.axf > ${BIN_DIR}/bl2.asm
        COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py pad
                -i ${BIN_DIR}/bl2.bin
                -o ${BIN_DIR}/xip_boot.bin  # prepend_header need this name
                -l 32
        COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py prepend_header
                -o ${BIN_DIR}/xip_boot_prepend.bin
                -i ${BIN_DIR}/xip_boot.bin
                -s __km4tz_boot_text_start__ -m ${BIN_DIR}/bl2.raw.map
                --boot-index 0x01010101
        COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py prepend_header
                -o ${BIN_DIR}/ram_1_prepend.bin
                -i ${BIN_DIR}/ram_1.bin
                -s __km4tz_boot_text_start__ -m ${BIN_DIR}/bl2.raw.map
        COMMAND ${CMAKE_COMMAND} -E cat ${BIN_DIR}/xip_boot_prepend.bin ${BIN_DIR}/ram_1_prepend.bin
                > ${BIN_DIR}/boot.bin
        COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py
                --post-build-dir ${CMAKE_BINARY_DIR}/amebagreen2_gcc_project
                fw_pack
                -o ${BIN_DIR}/${TARGET_SOC}_boot.bin
                --image1 ${BIN_DIR}/boot.bin
        COMMAND ${CMAKE_COMMAND} -E copy ${BIN_DIR}/${TARGET_SOC}_boot.bin ${IMAGES_DIR}/

        COMMENT "Process bl2 image"
        # VERBATIM ensures that '$$' in symbol names are passed literally to the command without shell escaping
        VERBATIM
    )

    add_custom_target(bl2_postbuild ALL
        DEPENDS ${IMAGES_DIR}/${TARGET_SOC}_boot.bin
    )

    add_custom_command(
        OUTPUT ${BIN_DIR}/tfm_s.asm
        DEPENDS tfm_s_signed_bin platform_s
        BYPRODUCTS
            ${BIN_DIR}/tfm_s.raw.map
            ${BIN_DIR}/target_pure_tfm.axf
            ${IMAGES_DIR}/tfm_s_signed.bin
        COMMAND ${CMAKE_NM} ${BIN_DIR}/tfm_s.axf | sort > ${BIN_DIR}/tfm_s.raw.map
        COMMAND ${CMAKE_OBJDUMP} -d ${BIN_DIR}/tfm_s.axf > ${BIN_DIR}/tfm_s.asm
        COMMAND ${CMAKE_COMMAND} -E copy ${BIN_DIR}/tfm_s.axf ${BIN_DIR}/target_pure_tfm.axf
        COMMAND ${CMAKE_STRIP} ${BIN_DIR}/target_pure_tfm.axf
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE_DIR:bl2>/tfm_s_signed.bin ${IMAGES_DIR}/
        COMMENT "Post process tfm_s target ${CMAKE_BINARY_DIR}"
        # VERBATIM ensures that '$$' in symbol names are passed literally to the command without shell escaping
        VERBATIM
    )
else()
    # Auto-generate disassembly file after tfm_s.axf is built
    add_custom_command(
        OUTPUT ${BIN_DIR}/tfm_s.asm ${BIN_DIR}/km4tz_image3_all.bin
        DEPENDS tfm_s
        COMMAND ${CMAKE_NM} ${BIN_DIR}/tfm_s.axf | sort > ${BIN_DIR}/tfm_s.raw.map
        COMMAND ${CMAKE_OBJDUMP} -d ${BIN_DIR}/tfm_s.axf > ${BIN_DIR}/tfm_s.asm
        COMMAND ${CMAKE_COMMAND} -E copy ${BIN_DIR}/tfm_s.axf ${BIN_DIR}/target_pure_tfm.axf
        COMMAND ${CMAKE_STRIP} ${BIN_DIR}/target_pure_tfm.axf
        COMMAND ${CMAKE_OBJCOPY} -j .TFM_VECTORS
                -Obinary ${BIN_DIR}/target_pure_tfm.axf ${BIN_DIR}/ram_3_s.bin
        COMMAND ${CMAKE_COMMAND} -E copy ${BIN_DIR}/tfm_s.bin ${BIN_DIR}/xip_3_s.bin
        COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py pad
                -i ${BIN_DIR}/ram_3_s.bin -l 32
        COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py pad
                -i ${BIN_DIR}/xip_3_s.bin -l 32

        COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py prepend_header
                -o ${BIN_DIR}/ram_3_s_prepend.bin
                -i ${BIN_DIR}/ram_3_s.bin
                -s __vectors_start__ -m ${BIN_DIR}/tfm_s.raw.map
        COMMAND ${PYTHON_EXECUTABLE} ${HAL_REALTEK_PATH}/ameba/scripts/axf2bin.py prepend_header
                -o ${BIN_DIR}/xip_3_s_prepend.bin
                -i ${BIN_DIR}/xip_3_s.bin
                -s Image$$PT_RO_START$$Base -m ${BIN_DIR}/tfm_s.raw.map

        COMMAND ${CMAKE_COMMAND} -E cat ${BIN_DIR}/xip_3_s_prepend.bin ${BIN_DIR}/ram_3_s_prepend.bin
                > ${BIN_DIR}/km4tz_image3_all.bin
        COMMENT "Generating tfm_s.asm disassembly file"
        # VERBATIM ensures that '$$' in symbol names are passed literally to the command without shell escaping
        VERBATIM
    )
endif()


add_custom_target(tfm_s_asm ALL
    DEPENDS ${BIN_DIR}/tfm_s.asm
)
