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

# Enable authenticated encryption (AES-256-GCM) of ITS files. The AEAD key is
# the on-chip IPSEC/OTP hardware key; the HAL is implemented in
# common/tfm_hal_its_encryption.c + rtl8721f_evb/ameba_hw_gcm.c.
set(ITS_ENCRYPTION                    ON           CACHE BOOL      "Enable authenticated encryption of ITS files using platform specific APIs")

# Use two separate image slots so MCUboot verifies S and NS independently.
# This matches the separate signing of tfm_s (256 KB slot) and the NS app
# (512 KB slot) already done by the Realtek platform build system.
set(MCUBOOT_IMAGE_NUMBER           2            CACHE STRING    "Number of independently-signed MCUboot images (S=0, NS=1)")

# Use FIH profile OFF so fih_int is a plain volatile int (not a struct),
# avoiding type-mismatch errors when BL2 platform code uses FIH_CALL macros.
# MEDIUM/HIGH profiles make fih_int a struct which is incompatible with the
# plain-int assignments and returns in TF-M platform helper code.
set(MCUBOOT_FIH_PROFILE               "OFF"        CACHE STRING    "MCUboot fault injection hardening profile")
set(MCUBOOT_SIGNATURE_TYPE            "EC-P256"    CACHE STRING    "Algorithm to use for signature validation [RSA-2048, RSA-3072, EC-P256, EC-P384]")
set(MCUBOOT_HW_KEY                    OFF          CACHE BOOL      "Whether to embed the entire public key in the image metadata instead of the hash only")
set(MCUBOOT_BUILTIN_KEY               ON           CACHE BOOL      "Use builtin key(s) for validation, no public key data is embedded into the image metadata")

set(HAL_REALTEK_PATH                  "DOWNLOAD"   CACHE PATH      "Path to hal_realtek (or DOWNLOAD to fetch automatically")
set(HAL_REALTEK_VERSION               "main"    CACHE STRING    "The version of hal_realtek to use")

set(MCUBOOT_HW_ROLLBACK_PROT            OFF          CACHE BOOL      "Enable security counter validation against non-volatile HW counters")
set(DEFAULT_MCUBOOT_SECURITY_COUNTERS   OFF          CACHE BOOL      "Whether to use the default security counter configuration defined by TF-M project")
set(MCUBOOT_SECURITY_COUNTER_S          1           CACHE STRING    "Security counter for S image. auto sets it to IMAGE_VERSION_S")

# Disable boot measurements: startup_bl2.c does not yet call boot_save_shared_data(),
# so the BOOT_DATA_AVAILABLE gate in tfm_boot_data.c would keep is_boot_data_valid=0
# and cause the Initial Attestation partition init to fail.
set(CONFIG_TFM_BOOT_STORE_MEASUREMENTS          OFF  CACHE BOOL      "Store boot measurements in shared data area")
set(CONFIG_TFM_BOOT_STORE_ENCODED_MEASUREMENTS  OFF  CACHE BOOL      "Store boot measurements in CBOR-encoded form")
