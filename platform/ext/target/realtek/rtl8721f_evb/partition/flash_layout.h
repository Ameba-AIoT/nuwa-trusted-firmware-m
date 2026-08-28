/*
 * Copyright (c) 2017-2022 Arm Limited. All rights reserved.
 * Copyright (c) 2020 Cypress Semiconductor Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __FLASH_LAYOUT_H__
#define __FLASH_LAYOUT_H__

/* Flash layout on RTL8721F (AmebaG2) without BL2:
 *
 * 0x0400_0000 Secure     image (512 KB)
 * 0x0408_0000 Non-secure image (512 KB)
 * 0x0410_0000 Protected Storage Area (20 KB)
 * 0x0410_5000 Internal Trusted Storage Area (16 KB)
 * 0x0410_9000 OTP / NV counters area (8 KB)
 *
 * Flash layout on RTL8721F (AmebaG2) with BL2 (single image boot):
 *
 * 0x0400_0000 BL2 - MCUBoot (80 KB)
 * 0x0401_4000 Primary image area (1 MB):
 *    0x0401_4000 Secure     image primary
 *    0x0409_4000 Non-secure image primary
 * 0x0411_4000 Secondary image area (1 MB):
 *    0x0411_4000 Secure     image secondary
 *    0x0419_4000 Non-secure image secondary
 * 0x0421_4000 Scratch area
 * 0x0421_4000 Protected Storage Area (20 KB)
 * 0x0421_9000 Internal Trusted Storage Area (16 KB)
 * 0x0421_D000 OTP / NV counters area (8 KB)
 */

/* This header file is included from linker scatter file as well, where only a
 * limited C constructs are allowed. Therefore it is not possible to include
 * here the platform_retarget.h to access flash related defines. To resolve this
 * some of the values are redefined here with different names, these are marked
 * with comment.
 */

/* Size of a Secure and of a Non-secure image */
#define FLASH_S_PARTITION_SIZE          (256 * 1024) /* S partition: 256 KB */
#define FLASH_NS_PARTITION_SIZE         (0x80000) /* NS partition: 512 KB */

#if (FLASH_S_PARTITION_SIZE > FLASH_NS_PARTITION_SIZE)
#define FLASH_MAX_PARTITION_SIZE FLASH_S_PARTITION_SIZE
#else
#define FLASH_MAX_PARTITION_SIZE FLASH_NS_PARTITION_SIZE
#endif
/* Sector size of the flash hardware; same as FLASH0_SECTOR_SIZE */
#define FLASH_AREA_IMAGE_SECTOR_SIZE    (0x1000)     /* 4 KB */
/* Same as FLASH0_SIZE */
#define FLASH_TOTAL_SIZE                (0x00400000) /* 4 MB */

/* Flash layout info for BL2 bootloader */
/* RTL8721F Flash base address */
#ifdef BL2
#define FLASH_BASE_ADDRESS              (0x103FF000)
#define FLASH_PHY_BASE_ADDRESS          (0x08000000)
#else
#define FLASH_BASE_ADDRESS          (0x08000000)
#endif

/* Offset and size definitions of the flash partitions that are handled by the
 * bootloader. The image swapping is done between IMAGE_PRIMARY and
 * IMAGE_SECONDARY, SCRATCH is used as a temporary storage during image
 * swapping.
 */
#define FLASH_AREA_BL2_OFFSET      (0x0)
#define FLASH_AREA_BL2_SIZE        (0x40000) /* 256 KB - matches RTL8721F bootloader */

#if !defined(MCUBOOT_IMAGE_NUMBER) || (MCUBOOT_IMAGE_NUMBER == 1)
/* Secure + Non-secure image primary slot */
#define FLASH_AREA_0_ID            (1)
#define FLASH_AREA_0_OFFSET        (FLASH_AREA_BL2_OFFSET + FLASH_AREA_BL2_SIZE)
#define FLASH_AREA_0_SIZE          (FLASH_S_PARTITION_SIZE + \
                                    FLASH_NS_PARTITION_SIZE)
/* Secure + Non-secure secondary slot */
#define FLASH_AREA_2_ID            (FLASH_AREA_0_ID + 1)
#define FLASH_AREA_2_OFFSET        (FLASH_AREA_0_OFFSET + FLASH_AREA_0_SIZE)
#define FLASH_AREA_2_SIZE          (FLASH_S_PARTITION_SIZE + \
                                    FLASH_NS_PARTITION_SIZE)
/* Scratch area */
#define FLASH_AREA_SCRATCH_ID      (FLASH_AREA_2_ID + 1)
#define FLASH_AREA_SCRATCH_OFFSET  (FLASH_AREA_2_OFFSET + FLASH_AREA_2_SIZE)
#define FLASH_AREA_SCRATCH_SIZE    (FLASH_S_PARTITION_SIZE + \
                                    FLASH_NS_PARTITION_SIZE)
/* The maximum number of status entries supported by the bootloader. */
#define MCUBOOT_STATUS_MAX_ENTRIES ((FLASH_S_PARTITION_SIZE + \
                                     FLASH_NS_PARTITION_SIZE) / \
                                    FLASH_AREA_SCRATCH_SIZE)
/* Maximum number of image sectors supported by the bootloader. */
#define MCUBOOT_MAX_IMG_SECTORS    ((FLASH_S_PARTITION_SIZE + \
                                     FLASH_NS_PARTITION_SIZE) / \
                                    FLASH_AREA_IMAGE_SECTOR_SIZE)
#elif (MCUBOOT_IMAGE_NUMBER == 2)
/* Secure image primary slot */
#define FLASH_AREA_0_ID            (1)
#define FLASH_AREA_0_OFFSET        (FLASH_AREA_BL2_OFFSET + FLASH_AREA_BL2_SIZE)
#define FLASH_AREA_0_SIZE          (FLASH_S_PARTITION_SIZE)
/* Non-secure image primary slot */
#define FLASH_AREA_1_ID            (FLASH_AREA_0_ID + 1)
#define FLASH_AREA_1_OFFSET        (FLASH_AREA_0_OFFSET + FLASH_AREA_0_SIZE)
#define FLASH_AREA_1_SIZE          (FLASH_NS_PARTITION_SIZE)
/* Secure image secondary slot */
#define FLASH_AREA_2_ID            (FLASH_AREA_1_ID + 1)
#define FLASH_AREA_2_OFFSET        (FLASH_AREA_1_OFFSET + FLASH_AREA_1_SIZE)
#define FLASH_AREA_2_SIZE          (FLASH_S_PARTITION_SIZE)
/* Non-secure image secondary slot */
#define FLASH_AREA_3_ID            (FLASH_AREA_2_ID + 1)
#define FLASH_AREA_3_OFFSET        (FLASH_AREA_2_OFFSET + FLASH_AREA_2_SIZE)
#define FLASH_AREA_3_SIZE          (FLASH_NS_PARTITION_SIZE)
/* Scratch area */
#define FLASH_AREA_SCRATCH_ID      (FLASH_AREA_3_ID + 1)
#define FLASH_AREA_SCRATCH_OFFSET  (FLASH_AREA_3_OFFSET + FLASH_AREA_3_SIZE)
#define FLASH_AREA_SCRATCH_SIZE    (FLASH_MAX_PARTITION_SIZE)
/* The maximum number of status entries supported by the bootloader. */
#define MCUBOOT_STATUS_MAX_ENTRIES (FLASH_MAX_PARTITION_SIZE / \
                                    FLASH_AREA_SCRATCH_SIZE)
/* Maximum number of image sectors supported by the bootloader. */
#define MCUBOOT_MAX_IMG_SECTORS    (FLASH_MAX_PARTITION_SIZE / \
                                    FLASH_AREA_IMAGE_SECTOR_SIZE)
#else /* MCUBOOT_IMAGE_NUMBER > 2 */
#error "Only MCUBOOT_IMAGE_NUMBER 1 and 2 are supported!"
#endif /* MCUBOOT_IMAGE_NUMBER */

/* Protected Storage (PS) Service definitions */
#define FLASH_PS_AREA_OFFSET            (0x00256000)
#define FLASH_PS_AREA_SIZE              (0x5000)   /* 20 KB */

/* Internal Trusted Storage (ITS) Service definitions */
#define FLASH_ITS_AREA_OFFSET           (FLASH_PS_AREA_OFFSET + \
                                         FLASH_PS_AREA_SIZE)
#define FLASH_ITS_AREA_SIZE             (0x4000)   /* 16 KB */

/* OTP_definitions */
#define FLASH_OTP_NV_COUNTERS_AREA_OFFSET (FLASH_ITS_AREA_OFFSET + \
                                           FLASH_ITS_AREA_SIZE)
#define FLASH_OTP_NV_COUNTERS_AREA_SIZE   (FLASH_AREA_IMAGE_SECTOR_SIZE * 2)
#define FLASH_OTP_NV_COUNTERS_SECTOR_SIZE FLASH_AREA_IMAGE_SECTOR_SIZE

/* Offset and size definition in flash area used by assemble.py */
#define SECURE_IMAGE_OFFSET             (0x0)
#define SECURE_IMAGE_MAX_SIZE           FLASH_S_PARTITION_SIZE

#define NON_SECURE_IMAGE_OFFSET         (SECURE_IMAGE_OFFSET + \
                                         SECURE_IMAGE_MAX_SIZE)
#define NON_SECURE_IMAGE_MAX_SIZE       FLASH_NS_PARTITION_SIZE

/* Flash device name used by BL2
 * Name is defined in flash driver file: Driver_Flash.c
 */
#define FLASH_DEV_NAME Driver_FLASH0
/* Smallest flash programmable unit in bytes */
#define TFM_HAL_FLASH_PROGRAM_UNIT       (0x1)

/* Protected Storage (PS) Service definitions
 * Note: Further documentation of these definitions can be found in the
 * TF-M PS Integration Guide.
 */
#define TFM_HAL_PS_FLASH_DRIVER Driver_FLASH0

/* In this target the CMSIS driver requires only the offset from the base
 * address instead of the full memory address.
 */
/* Base address of dedicated flash area for PS */
#define TFM_HAL_PS_FLASH_AREA_ADDR    FLASH_PS_AREA_OFFSET
/* Size of dedicated flash area for PS */
#define TFM_HAL_PS_FLASH_AREA_SIZE    FLASH_PS_AREA_SIZE
#define PS_RAM_FS_SIZE                TFM_HAL_PS_FLASH_AREA_SIZE
/* Number of physical erase sectors per logical FS block */
#define TFM_HAL_PS_SECTORS_PER_BLOCK  (1)
/* Smallest flash programmable unit in bytes */
#define TFM_HAL_PS_PROGRAM_UNIT       (0x1)

/* Internal Trusted Storage (ITS) Service definitions
 * Note: Further documentation of these definitions can be found in the
 * TF-M ITS Integration Guide. The ITS should be in the internal flash, but is
 * allocated in the external flash just for development platforms that don't
 * have internal flash available.
 */
#define TFM_HAL_ITS_FLASH_DRIVER Driver_FLASH0

/* In this target the CMSIS driver requires only the offset from the base
 * address instead of the full memory address.
 */
/* Base address of dedicated flash area for ITS */
#define TFM_HAL_ITS_FLASH_AREA_ADDR    FLASH_ITS_AREA_OFFSET
/* Size of dedicated flash area for ITS */
#define TFM_HAL_ITS_FLASH_AREA_SIZE    FLASH_ITS_AREA_SIZE
#define ITS_RAM_FS_SIZE                TFM_HAL_ITS_FLASH_AREA_SIZE
/* Number of physical erase sectors per logical FS block */
#define TFM_HAL_ITS_SECTORS_PER_BLOCK  (1)
/* Smallest flash programmable unit in bytes */
#define TFM_HAL_ITS_PROGRAM_UNIT       (0x1)

/* OTP / NV counter definitions */
#define TFM_OTP_NV_COUNTERS_AREA_SIZE   (FLASH_OTP_NV_COUNTERS_AREA_SIZE / 2)
#define TFM_OTP_NV_COUNTERS_AREA_ADDR   FLASH_OTP_NV_COUNTERS_AREA_OFFSET
#define TFM_OTP_NV_COUNTERS_SECTOR_SIZE FLASH_OTP_NV_COUNTERS_SECTOR_SIZE
#define TFM_OTP_NV_COUNTERS_BACKUP_AREA_ADDR (TFM_OTP_NV_COUNTERS_AREA_ADDR + \
                                              TFM_OTP_NV_COUNTERS_AREA_SIZE)

/* RTL8721F (AmebaG2) memory aliases */
#define S_ROM_ALIAS_BASE  (0x00C00020)  /* Secure Flash base (XIP virtual address, no S-bit prefix) */
#define NS_ROM_ALIAS_BASE (0x04000020)  /* Non-Secure Flash base (same physical flash) */

/* ===================== SRAM partitioning ==============================
 *
 * Only the SRAM_* primitives below and S_DATA_SIZE are chosen by hand;
 * everything else in this block, in region_defs.h, and the BL2/S/NS linker
 * scripts is derived from them. The `#error` guards at the end of
 * region_defs.h fail the build if a primitive stops adding up.
 *
 * The windows mirror amebag2/ameba_layout.ld, which defines them cumulatively
 * so they structurally cannot overlap:
 *
 *   KM4TZ_IMG1_RAM_SATRT   = SRAM_BASE + KM4TZ_KM4NS_RAM_SIZE  = 0x20005000
 *   KM4TZ_IMG1_SIZE        = 4 KB  if CONFIG_IMG1_FLASH (bootloader XIPs from flash)
 *                            32 KB otherwise            (bootloader resident in RAM)
 *   KM4TZ_RAM_TZ_NSC_START = KM4TZ_IMG1_RAM_SATRT + KM4TZ_IMG1_SIZE  <- next image
 *   KM4TZ_BOOTLOADER_RAM_S = KM4TZ_IMG1_RAM_SATRT + SECURE_ADDR_OFFSET(0x10000000)
 *                                                  + IMAGE_HEADER_LEN(0x20)
 *
 * TF-M's BL2 XIPs its code from flash but still needs ~26 KB of RAM (mcuboot +
 * mbedtls + stack + heap), i.e. the SDK's 32 KB variant, so the image above it
 * starts at 0x2000D000. The earlier 0x20006020 secure base was the 4 KB
 * (flash-XIP bootloader) boundary, which only holds if the bootloader's *data*
 * also fits in 4 KB -- it does not, so the secure image landed inside BL2's own
 * .bss / stack / heap.
 *
 *   0x20000000 ..                 ROM BSS, MSP stacks, IPC shared (fixed HW)
 *   SRAM_BL2_START      + 0x20    ameba image header
 *                       ..        BL2 .data/.bss/.msp_stack/.heap
 *   S_RAM_ALIAS_BASE    ..        TF-M secure image RAM        (S_DATA_SIZE)
 *   NS_RAM_ALIAS_BASE   ..        non-secure image (Zephyr)    (NS_DATA_SIZE)
 *   SRAM_NS_TOP                   hard top: KM4NS image base
 */
#define SRAM_SECURE_ALIAS_OFFSET (0x10000000)
#define SRAM_S_ALIAS(x)          ((x) + SRAM_SECURE_ALIAS_OFFSET)

#define SRAM_NS_TOP              (0x20068000)
#define SRAM_BL2_START           (0x20005000) /* SDK KM4TZ_IMG1_RAM_SATRT */
#define SRAM_BL2_SIZE            (0x8000)     /* SDK KM4TZ_IMG1_SIZE, RAM-resident */
#define SRAM_BL2_ENTRY_RESERVED  (0x20)       /* ameba image header; the ROM entry
                                               * table is in flash for this SoC */

/* Secure SRAM base == the SDK's KM4TZ_RAM_TZ_NSC_START: first address above BL2.
 * The secure image XIPs from flash (S_CODE_START), so this is its RAM only and
 * needs no image-header gap. */
#define S_RAM_ALIAS_BASE         (SRAM_BL2_START + SRAM_BL2_SIZE) /* 0x2000D000 */

/* Secure image RAM footprint.
 *  -  96 KB default (non-regression); covers standard partitions + IAT +
 *     custom partitions. Change 8964 (BL2 support) added ~10 KB S-side BSS when
 *     IAT is enabled; any extra partition also consumes S RAM.
 *  - 140 KB regression test (TFM_S_REG_TEST / TFM_NS_REG_TEST) */
#if defined(TFM_NS_REG_TEST) || defined(TFM_S_REG_TEST)
#define S_DATA_SIZE              (140 * 1024)
#else
#define S_DATA_SIZE              (96 * 1024)
#endif

/* Non-secure (Zephyr) SRAM base. The dts node `sram0_ns` in
 * dts/arm/realtek/amebag2/amebag2.dtsi is the one value that cannot be derived
 * from here and must be kept equal:
 *   S= 96 KB -> 0x20025000, size 0x43000 (268 KB)
 *   S=140 KB -> 0x20030000, size 0x38000 (224 KB) */
#define NS_RAM_ALIAS_BASE        (S_RAM_ALIAS_BASE + S_DATA_SIZE)

/* KM4TZ MSP_S RAM: 2.5k */
#define KM4TZ_MSP_RAM_S_ADDR  (0x30000600)
#define KM4TZ_MSP_RAM_S_SIZE  (0x30001000 - 0x30000600)

#define TOTAL_ROM_SIZE FLASH_TOTAL_SIZE
/* S+NS SRAM available to TF-M: S_RAM_ALIAS_BASE ~ SRAM_NS_TOP (0x5B000, 364 KB).
 * SRAM_NS_TOP = KM4NS_IMG2_RAM_START is a hard boundary — the second
 * independent core (KM4NS, no TrustZone) loads its firmware there.
 * NS_DATA_END = NS_DATA_START + NS_DATA_SIZE
 *   = (S_RAM_ALIAS_BASE + S_DATA_SIZE) + (TOTAL_RAM_SIZE - S_DATA_SIZE)
 *   = S_RAM_ALIAS_BASE + TOTAL_RAM_SIZE  ==  SRAM_NS_TOP  (exact). */
#define TOTAL_RAM_SIZE (SRAM_NS_TOP - S_RAM_ALIAS_BASE)

#define NS_AP_LOGIC_BASE (0x04000000)
#define NS_NP_LOGIC_BASE (0x02000000)

#endif /* __FLASH_LAYOUT_H__ */
