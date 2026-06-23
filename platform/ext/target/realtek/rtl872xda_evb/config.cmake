#-------------------------------------------------------------------------------
# Copyright (c) 2025, Realtek Semiconductor Corp.
#
# SPDX-License-Identifier: BSD-3-Clause
#
#-------------------------------------------------------------------------------

set(BL2                               OFF         CACHE BOOL      "Whether to build BL2")
set(NS                                OFF         CACHE BOOL      "Whether to build NS app")
set(PLATFORM_DEFAULT_UART_STDOUT      OFF         CACHE BOOL      "Use default uart stdout implementation.")
set(TFM_ISOLATION_LEVEL               2           CACHE STRING    "Isolation level")

# Platform-specific configurations

set(CONFIG_TFM_USE_TRUSTZONE          ON)
set(TFM_MULTI_CORE_TOPOLOGY           OFF)

set(PLATFORM_HAS_ISOLATION_L3_SUPPORT OFF)
if(BL2)
set(PLATFORM_DEFAULT_OTP                OFF        CACHE BOOL     "Use trusted on-chip flash to implement OTP memory")
endif()


set(MCUBOOT_USE_PSA_CRYPTO            ON           CACHE BOOL      "Enable the cryptographic abstraction layer to use PSA Crypto APIs")
set(MCUBOOT_SIGNATURE_TYPE            "EC-P256"    CACHE STRING    "Algorithm to use for signature validation [RSA-2048, RSA-3072, EC-P256, EC-P384]")
set(MCUBOOT_HW_KEY                    OFF          CACHE BOOL      "Whether to embed the entire public key in the image metadata instead of the hash only")
set(MCUBOOT_BUILTIN_KEY               ON           CACHE BOOL      "Use builtin key(s) for validation, no public key data is embedded into the image metadata")


set(HAL_REALTEK_PATH                  "DOWNLOAD"   CACHE PATH      "Path to hal_realtek (or DOWNLOAD to fetch automatically")
set(HAL_REALTEK_VERSION               "main"    CACHE STRING    "The version of hal_realtek to use")

set(TFM_SPM_LOG_LEVEL                      TFM_SPM_LOG_LEVEL_DEBUG         CACHE STRING    "Set default SPM log level as INFO level" FORCE)
set(TFM_PARTITION_LOG_LEVEL                TFM_PARTITION_LOG_LEVEL_DEBUG   CACHE STRING    "Set default Secure Partition log level as INFO level" FORCE)

set(MCUBOOT_HW_ROLLBACK_PROT            OFF          CACHE BOOL      "Enable security counter validation against non-volatile HW counters")
set(DEFAULT_MCUBOOT_SECURITY_COUNTERS   OFF          CACHE BOOL      "Whether to use the default security counter configuration defined by TF-M project")
set(MCUBOOT_SECURITY_COUNTER_S          1           CACHE STRING    "Security counter for S image. auto sets it to IMAGE_VERSION_S")
