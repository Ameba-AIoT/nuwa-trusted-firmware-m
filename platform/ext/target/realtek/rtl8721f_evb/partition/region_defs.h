/*
 * Copyright (c) 2017-2022 Arm Limited. All rights reserved.
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

#ifndef __REGION_DEFS_H__
#define __REGION_DEFS_H__

#include "flash_layout.h"

#define BL2_HEAP_SIZE           (0x00001000)
#define BL2_MSP_STACK_SIZE      (0x00001800)

#ifdef ENABLE_HEAP
#define S_HEAP_SIZE             (0x00000200)
#endif

#ifdef TFM_FIH_PROFILE_ON
#define S_MSP_STACK_SIZE        (0x00000A40)
#else
#define S_MSP_STACK_SIZE        (0x00000800)
#endif
#define S_PSP_STACK_SIZE        (0x00000800)

#define NS_HEAP_SIZE            (0x00001000)
#define NS_STACK_SIZE           (0x000001E0)

/* RTL8721F (AmebaG2) memory region definitions */

#ifdef BL2
#ifndef LINK_TO_SECONDARY_PARTITION
#define S_IMAGE_PRIMARY_PARTITION_OFFSET   (FLASH_AREA_0_OFFSET)
#define S_IMAGE_SECONDARY_PARTITION_OFFSET (FLASH_AREA_2_OFFSET)
#else
#define S_IMAGE_PRIMARY_PARTITION_OFFSET   (FLASH_AREA_2_OFFSET)
#define S_IMAGE_SECONDARY_PARTITION_OFFSET (FLASH_AREA_0_OFFSET)
#endif /* !LINK_TO_SECONDARY_PARTITION */
#else
#define S_IMAGE_PRIMARY_PARTITION_OFFSET (0x0)
#endif /* BL2 */

#ifndef LINK_TO_SECONDARY_PARTITION
#define NS_IMAGE_PRIMARY_PARTITION_OFFSET (FLASH_AREA_0_OFFSET \
                                           + FLASH_S_PARTITION_SIZE)
#else
#define NS_IMAGE_PRIMARY_PARTITION_OFFSET (FLASH_AREA_2_OFFSET \
                                           + FLASH_S_PARTITION_SIZE)
#endif /* !LINK_TO_SECONDARY_PARTITION */

/* Boot partition structure if MCUBoot is used:
 * 0x0_0000 Bootloader header
 * 0x0_0400 Image area
 * 0x7_0000 Trailer
 */
/* IMAGE_CODE_SIZE is the space available for the software binary image.
 * It is less than the FLASH_S_PARTITION_SIZE + FLASH_NS_PARTITION_SIZE
 * because we reserve space for the image header and trailer introduced
 * by the bootloader.
 */

#if (!defined(MCUBOOT_IMAGE_NUMBER) || (MCUBOOT_IMAGE_NUMBER == 1)) && \
    (NS_IMAGE_PRIMARY_PARTITION_OFFSET > S_IMAGE_PRIMARY_PARTITION_OFFSET)
/* If secure image and nonsecure image are concatenated, and nonsecure image
 * locates at the higher memory range, then the secure image does not need
 * the trailer area.
 */
#define IMAGE_S_CODE_SIZE \
            (FLASH_S_PARTITION_SIZE - BL2_HEADER_SIZE)
#else
#define IMAGE_S_CODE_SIZE \
            (FLASH_S_PARTITION_SIZE - BL2_HEADER_SIZE - BL2_TRAILER_SIZE)
#endif

#define IMAGE_NS_CODE_SIZE \
            (FLASH_NS_PARTITION_SIZE - BL2_HEADER_SIZE - BL2_TRAILER_SIZE)

/* Alias definitions for secure and non-secure areas*/
#define S_ROM_ALIAS(x)  (S_ROM_ALIAS_BASE + (x))
#define NS_ROM_ALIAS(x) (NS_ROM_ALIAS_BASE + (x))

#define S_RAM_ALIAS(x)  (S_RAM_ALIAS_BASE + (x))
#define NS_RAM_ALIAS(x) (NS_RAM_ALIAS_BASE + (x))

/* Secure regions */
#define S_IMAGE_PRIMARY_AREA_OFFSET \
             (S_IMAGE_PRIMARY_PARTITION_OFFSET + BL2_HEADER_SIZE)
#define S_CODE_START    (S_ROM_ALIAS(S_IMAGE_PRIMARY_AREA_OFFSET))
#define S_CODE_SIZE     (IMAGE_S_CODE_SIZE)
#define S_CODE_LIMIT    (S_CODE_START + S_CODE_SIZE - 1)

#define S_DATA_START    (S_RAM_ALIAS(0x0))
/* S_DATA_SIZE lives in flash_layout.h next to the SRAM_* primitives it
 * partitions, so NS_RAM_ALIAS_BASE can be derived from it. */
#define S_DATA_LIMIT    (S_DATA_START + S_DATA_SIZE - 1)

/* Non-secure regions */
#define NS_IMAGE_PRIMARY_AREA_OFFSET \
                        (NS_IMAGE_PRIMARY_PARTITION_OFFSET + BL2_HEADER_SIZE)
/* In BL2 mode the signed NS binary is loaded to XIP at NS_AP_LOGIC_BASE
 * (0x04000000) by img1.  The first BL2_HEADER_SIZE bytes = MCUboot header.
 * NS code is linked from NS_AP_LOGIC_BASE + BL2_HEADER_SIZE so that the .vt_bl2
 * stub (placed first in .text) lands exactly where tfm_hal_get_ns_MSP() and
 * tfm_hal_get_ns_entry_point() read from.
 * Note: this definition assumes MCUBOOT_IMAGE_NUMBER=2 (separate NS slot). */
#ifdef BL2
#define NS_CODE_START   (NS_AP_LOGIC_BASE + BL2_HEADER_SIZE)
#else
#define NS_CODE_START   (NS_ROM_ALIAS(0))
#endif
#define NS_CODE_SIZE    (IMAGE_NS_CODE_SIZE)
#define NS_CODE_LIMIT   (NS_CODE_START + NS_CODE_SIZE - 1)

#define NS_DATA_START   (NS_RAM_ALIAS(0x0))
#if defined(PSA_API_TEST_NS) && !defined(PSA_API_TEST_IPC)
#define DEV_APIS_TEST_NVMEM_REGION_SIZE  0x400
#define NS_DATA_SIZE    (TOTAL_RAM_SIZE - S_DATA_SIZE - DEV_APIS_TEST_NVMEM_REGION_SIZE)
#else
#define NS_DATA_SIZE    (TOTAL_RAM_SIZE - S_DATA_SIZE)
#endif
#define NS_DATA_LIMIT   (NS_DATA_START + NS_DATA_SIZE - 1)

/* NS partition information is used for MPC and SAU configuration */
#define NS_PARTITION_START \
            (NS_ROM_ALIAS(NS_IMAGE_PRIMARY_PARTITION_OFFSET))
#define NS_PARTITION_SIZE (FLASH_NS_PARTITION_SIZE)

/* Secondary partition for new images in case of firmware upgrade */
#define SECONDARY_PARTITION_START \
            (NS_ROM_ALIAS(S_IMAGE_SECONDARY_PARTITION_OFFSET))
#define SECONDARY_PARTITION_SIZE (FLASH_S_PARTITION_SIZE + \
                                  FLASH_NS_PARTITION_SIZE)

#ifdef BL2
/* Bootloader regions */
#define BL2_CODE_START    (0x10400020)
#define BL2_CODE_SIZE     (FLASH_AREA_BL2_SIZE)
#define BL2_CODE_LIMIT    (BL2_CODE_START + BL2_CODE_SIZE - 1)

/* BL2 data fills the SDK's IMG1 (bootloader) RAM window from the end of the
 * image header up to the secure image. Derived, so it can never overlap the
 * secure image; see the SRAM partitioning block in flash_layout.h for the map.
 *
 * The overlap matters on the power-gate wake path, which re-enters BL2 without
 * re-running Reset_Handler: BL2 would find secure-world content where its .bss
 * is, and the loader code it runs there dispatches through .bss function
 * pointers. It also makes boot_clear_ram_area() unsafe (it would scrub part of
 * the secure image on the way out of BL2). */
#define BL2_DATA_END      (S_RAM_ALIAS_BASE)
#define BL2_DATA_START    (SRAM_BL2_START + SRAM_BL2_ENTRY_RESERVED)
#define BL2_DATA_SIZE     (BL2_DATA_END - BL2_DATA_START)
#define BL2_DATA_LIMIT    (BL2_DATA_START + BL2_DATA_SIZE - 1)
#endif /* BL2 */



/* Shared symbol area between bootloader and runtime firmware. Global variables
 * in the shared code can be placed here.
 * Note: For RTL8721F, we need to use a valid RAM address within S_RAM region.
 */
#ifdef CODE_SHARING
#define SHARED_SYMBOL_AREA_BASE (S_RAM_ALIAS_BASE + 0x1000)
#define SHARED_SYMBOL_AREA_SIZE 0x20
#else
#define SHARED_SYMBOL_AREA_BASE (S_RAM_ALIAS_BASE + 0x1000)
#define SHARED_SYMBOL_AREA_SIZE 0x0
#endif /* CODE_SHARING */

/* Shared data area between bootloader and runtime firmware.
 * These areas are allocated at the beginning of the RAM, it is overlapping
 * with TF-M Secure code's MSP stack
 */
#define BOOT_TFM_SHARED_DATA_BASE (SHARED_SYMBOL_AREA_BASE + \
                                   SHARED_SYMBOL_AREA_SIZE)
#define BOOT_TFM_SHARED_DATA_SIZE (0x400)
#define BOOT_TFM_SHARED_DATA_LIMIT (BOOT_TFM_SHARED_DATA_BASE + \
                                    BOOT_TFM_SHARED_DATA_SIZE - 1)
#define SHARED_BOOT_MEASUREMENT_BASE BOOT_TFM_SHARED_DATA_BASE
#define SHARED_BOOT_MEASUREMENT_SIZE BOOT_TFM_SHARED_DATA_SIZE
#define SHARED_BOOT_MEASUREMENT_LIMIT BOOT_TFM_SHARED_DATA_LIMIT

/* KM4TZ MSP_S RAM */
#define S_KM4TZ_MSP_RAM_ADDR KM4TZ_MSP_RAM_S_ADDR
#define S_KM4TZ_MSP_RAM_SIZE KM4TZ_MSP_RAM_S_SIZE

/*------------------------- SRAM layout invariants ---------------------------
 * These hold by construction as long as everything is derived from the SRAM_*
 * primitives in flash_layout.h. They exist to fail the build the moment a
 * value is hardcoded again and stops agreeing with the rest.
 */
#if (S_RAM_ALIAS_BASE) < ((SRAM_BL2_START) + (SRAM_BL2_SIZE))
#error "S_RAM_ALIAS_BASE overlaps BL2's RAM window: BL2 cannot be re-entered on the power-gate wake path, and boot_clear_ram_area() would scrub the secure image"
#endif

#if (NS_RAM_ALIAS_BASE) != ((S_RAM_ALIAS_BASE) + (S_DATA_SIZE))
#error "NS_RAM_ALIAS_BASE must be S_RAM_ALIAS_BASE + S_DATA_SIZE (dts sram0_ns must match too)"
#endif

#if !defined(PSA_API_TEST_NS) || defined(PSA_API_TEST_IPC)
#if ((NS_DATA_START) + (NS_DATA_SIZE)) != (SRAM_NS_TOP)
#error "non-secure data must end exactly at SRAM_NS_TOP"
#endif
#endif

#ifdef BL2
#if ((BL2_DATA_START) + (BL2_DATA_SIZE)) > (BL2_DATA_END)
#error "BL2 data runs into the secure image"
#endif
#if (BL2_DATA_SIZE) < (0x7000)
#error "BL2 RAM window too small: TF-M's BL2 needs ~26 KB (mcuboot + mbedtls + stack + heap)"
#endif
#endif /* BL2 */

#endif /* __REGION_DEFS_H__ */
