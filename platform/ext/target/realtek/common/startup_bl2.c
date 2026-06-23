/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Startup code for Realtek Ameba TF-M BL2 (shared by amebag2 / amebadplus)
 */

#include "boot_prepare.h"
#include "tfm_hal_device_header.h"
#include "boot_hal.h"
#include "tfm_plat_defs.h"
#include "fih.h"
#include "bootutil/bootutil.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/image.h"
#include <sysflash/sysflash.h>
#ifdef CONFIG_AMEBADPLUS
#include "region_defs.h"
extern ARM_DRIVER_FLASH Driver_FLASH0;

/* Override the (__WEAK) upstream boot_clear_ram_area. The default impl
 * zeroes BL2's [Image$$ER_DATA$$Base, Image$$ARM_LIB_HEAP$$ZI$$Limit),
 * which on amebadplus lives at 0x3000A520-0x30010780 — the SAME physical
 * RAM as 0x2000A520-0x20010780 (the 0x2xxxxxxx and 0x3xxxxxxx views differ
 * only in the security-attribute address bit, but back the same physical
 * memory). Since the TFM-S image is loaded into RAM at S_RAM_ALIAS_BASE =
 * 0x2000B020 and extends well past 0x20010780, the upstream clear wipes
 * the first ~22 KB of the freshly-copied TFM-S image (vector table, SP
 * load list, and the start of TFM_PSA_ROT_LINKER which contains
 * platform_sp_init) before the jump. The result is RAM = 0 at TFM-S
 * runtime even though the BL2 copy succeeded. We don't actually need to
 * scrub BL2's RAM (no secrets there), so skip it. */
void boot_clear_ram_area(void)
{
}
#endif


/* NOTE: When using tfm bl2, image 0 is secure image, image 1 is app(non-secure) image */
#define APP_IMAGE_AREA_ID FLASH_AREA_IMAGE_PRIMARY(1)
#define APP_IMAGE_AREA_OFFSET FLASH_AREA_1_OFFSET
#define APP_IMAGE_AREA_SIZE FLASH_AREA_1_SIZE

#define IMG_AP_LOGIC_ADDR   NS_AP_LOGIC_BASE				/* 0x04000000 */
#define IMG_NP_LOGIC_ADDR   NS_NP_LOGIC_BASE				/* 0x02000000 */
#define FLASH_BASE_PHY_ADDR FLASH_PHY_BASE_ADDRESS	/* 0x08000000 */

/*----------------------------------------------------------------------------
  External References
 *----------------------------------------------------------------------------*/
extern uint32_t __INITIAL_SP;
extern uint32_t __STACK_LIMIT;
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
extern uint64_t __STACK_SEAL;
#endif

extern __NO_RETURN void __PROGRAM_START(void);

/*----------------------------------------------------------------------------
  Internal References
 *----------------------------------------------------------------------------*/
__NO_RETURN void Reset_Handler (void);

/*----------------------------------------------------------------------------
  Reset Handler called on controller reset
 *----------------------------------------------------------------------------*/
void Reset_Handler(void)
{
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    __disable_irq();
#endif
    __set_PSP((uint32_t)(&__INITIAL_SP));

    __set_MSPLIM((uint32_t)(&__STACK_LIMIT));
    __set_PSPLIM((uint32_t)(&__STACK_LIMIT));

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    __TZ_set_STACKSEAL_S((uint32_t *)(&__STACK_SEAL));
#endif

    SystemInit();                             /* CMSIS System Initialization */
    __PROGRAM_START();                        /* Enter PreMain (C library entry point) */
}

int boot_platform_post_load(uint32_t image_id)
{
	/*NOTE: image_id with value 0 represents the APP image, 1 is secure image(tfm_s)  */
	if (image_id != 0) {
		return 0;
	}

	boot_prepare(
		FLASH_BASE_PHY_ADDR,
		APP_IMAGE_AREA_ID,
		APP_IMAGE_AREA_OFFSET,
		APP_IMAGE_AREA_SIZE,
		IMG_AP_LOGIC_ADDR,
		IMG_NP_LOGIC_ADDR
	);

	return 0;
}

int flash_device_base(uint8_t fd_id, uintptr_t *ret)
{
	switch (fd_id) {
	case FLASH_DEVICE_ID :
		*ret = FLASH_BASE_PHY_ADDR;
		break;
	default:
		RTK_LOGE(NOTAG, "invalid flash ID %d; expected %d",
				 fd_id, FLASH_DEVICE_ID);
		*ret = -1;
		return -1;
	}
	return 0;
}

#ifdef CONFIG_AMEBADPLUS
/*
 * The TFM S image is signed with IMAGE_F_RAM_LOAD and S_IMAGE_LOAD_ADDRESS
 * pointing to S_DATA_START — but the MCUBoot OVERWRITE_ONLY strategy does
 * not actually relocate the payload. We do the load here, as the very last
 * step before jumping into S, so it can't perturb boot_prepare's setup or
 * BL2's stack frames.
 *
 * Use a word-by-word volatile loop: the ROM _memcpy crashes on transfers
 * larger than ~64KB from the RSIP-mapped XIP window. RSIP virtual region
 * [0x0F800000, 0x0FFFFFE0) remains accessible after BOOT_RAM_TZCfg because
 * BL2 itself continues to XIP from it.
 */
void boot_platform_start_next_image(struct boot_arm_vector_table *vt)
{
	const struct image_header *s_hdr;
	volatile const uint32_t *src;
	volatile uint32_t *dst;
	uint32_t words;

	s_hdr = (const struct image_header *)(FLASH_BASE_ADDRESS +
					      S_IMAGE_PRIMARY_PARTITION_OFFSET);
	src = (const uint32_t *)((uintptr_t)s_hdr + s_hdr->ih_hdr_size);
	dst = (uint32_t *)S_DATA_START;
	words = (s_hdr->ih_img_size + 3) / 4;

	BOOT_LOG_DBG("S img: hdr=0x%x sz=%u -> 0x%x",
		     (unsigned)s_hdr, (unsigned)s_hdr->ih_img_size,
		     (unsigned)S_DATA_START);

	/* Copy the TFM-S image from RSIP-mapped flash to RAM, then make it
	 * visible to TFM-S:
	 *  - DCache_CleanInvalidate (by-MVA over the destination range):
	 *    writes any dirty D-cache lines covering the range back to RAM
	 *    and drops them, so the next read fetches fresh data from RAM.
	 *  - ICIALLU: invalidate the entire I-cache so the freshly-written
	 *    instructions are pulled from RAM on the next fetch. */
	{
		extern void DCache_CleanInvalidate(uint32_t addr, uint32_t bytes);
		for (uint32_t i = 0; i < words; i++) {
			dst[i] = src[i];
		}
		__DSB();
		__ISB();
		DCache_CleanInvalidate((uint32_t)dst, words * 4);
		SCB->ICIALLU = 0;
		__DSB();
		__ISB();
	}

	BOOT_LOG_DBG("vt->msp=0x%x vt->reset=0x%x",
		     (unsigned)vt->msp, (unsigned)vt->reset);

	Driver_FLASH0.Uninitialize();

	__set_MSPLIM(0);
	__set_MSP(vt->msp);
	__DSB();
	__ISB();
	boot_jump_to_next_image(vt->reset);
}
#endif /* CONFIG_AMEBADPLUS */

