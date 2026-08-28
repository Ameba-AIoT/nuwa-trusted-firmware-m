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
#include "region_defs.h"
#include <ameba_soc.h>		/* RomVectorTable */
#if defined(SOC_AMEBAG2)
#include "boot_security_km4tz.h"	/* BOOT_Wake_TZCfg() */
#elif defined(SOC_AMEBADPLUS)
#include "bootloader_km4.h"	/* BOOT_TRNG_ParaSet(), BOOT_RAM_TZCfg() */
/* Defined in bootloader_km4.c, which declares it in no header. */
extern void BOOT_SCBConfig_HP(void);
#endif

/*
 * Two flavours of secure image, distinguished by the partition layout itself:
 * when S_IMAGE_LOAD_ADDRESS is defined the image is signed with
 * IMAGE_F_RAM_LOAD and BL2 copies it into RAM (AmebaDplus); otherwise it is
 * executed in place from flash (AmebaG2). The difference matters both at cold
 * boot and on the power-gate wake path below.
 */
#if defined(S_IMAGE_LOAD_ADDRESS)
#define S_IMAGE_VECTOR_TABLE  S_DATA_START
#else
#define S_IMAGE_VECTOR_TABLE  S_CODE_START
#endif

#if defined(S_IMAGE_LOAD_ADDRESS)
extern ARM_DRIVER_FLASH Driver_FLASH0;
#endif /* S_IMAGE_LOAD_ADDRESS */


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

#if defined(S_IMAGE_LOAD_ADDRESS)
/*
 * The TFM S image is signed with IMAGE_F_RAM_LOAD and S_IMAGE_LOAD_ADDRESS
 * pointing to S_DATA_START — but the MCUBoot OVERWRITE_ONLY strategy does
 * not actually relocate the payload. We do the load here, as the very last
 * step before jumping into S, so it can't perturb boot_prepare's setup.
 *
 * Use a word-by-word volatile loop: the ROM _memcpy crashes on transfers
 * larger than ~64KB from the RSIP-mapped XIP window. RSIP virtual region
 * [0x0F800000, 0x0FFFFFE0) remains accessible after BOOT_RAM_TZCfg because
 * BL2 itself continues to XIP from it.
 *
 * The destination starts at S_RAM_ALIAS_BASE, above BL2's RAM window, so it no
 * longer overlaps BL2's own data and stack (it did while the secure base was
 * derived from the SDK's 4 KB flash-XIP bootloader boundary; see the SRAM
 * partitioning block in flash_layout.h).
 */
static inline __attribute__((always_inline)) void boot_load_secure_image(void)
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
}

void boot_platform_start_next_image(struct boot_arm_vector_table *vt)
{
	boot_load_secure_image();

	BOOT_LOG_DBG("vt->msp=0x%x vt->reset=0x%x",
		     (unsigned)vt->msp, (unsigned)vt->reset);

	Driver_FLASH0.Uninitialize();

	__set_MSPLIM(0);
	__set_MSP(vt->msp);
	__DSB();
	__ISB();
	boot_jump_to_next_image(vt->reset);
}
#else  /* !S_IMAGE_LOAD_ADDRESS */
/* Executed in place from flash: nothing to load, and its initialised data is
 * restored from the flash LMA by the image's own startup copy table. */
#define boot_load_secure_image() do { } while (0)
#endif /* S_IMAGE_LOAD_ADDRESS */

/*
 * Enter the secure image exactly the way a cold boot ends: MSP and reset vector
 * come from its vector table.
 *
 * VTOR is deliberately left alone, as at cold boot: the vector table the secure
 * world runs with is not necessarily this one (on AmebaG2 the image's .vectors
 * holds only the initial SP and reset vector, and the secure handlers are
 * registered into the SoC's RAM vector table instead), and the image's own
 * startup installs whichever table it uses.
 */
static __attribute__((noreturn)) void boot_enter_secure_image(void)
{
	const uint32_t *vt;

	boot_load_secure_image();

	vt = (const uint32_t *)S_IMAGE_VECTOR_TABLE;

	__set_MSPLIM(0);
	__set_MSP(vt[0]);
	__DSB();
	__ISB();
	boot_jump_to_next_image(vt[1]);

	while (1) {
	}
}

/*
 * The power-gate wake entry, overriding the __weak BOOT_WakeFromPG() in the
 * loader (bootloader_km4tz.c / bootloader_km4.c). The ROM reaches it through
 * RamStartTable.RamWakeupFun -- not through Reset_Handler -- so this is the
 * first code to run after the AP is powered back on, and BL2 proper is not
 * re-run.
 *
 * The loader's own body re-arms the non-secure world and jumps straight into
 * the non-secure image, which is right when the loader is itself the secure
 * world. With TF-M resident there it is not: power-gating loses the whole core
 * state, including the secure world's VTOR, MPU, SAU and stacks, so TF-M has to
 * re-initialise itself. BL2 is not re-run for that -- it would reset the
 * peripherals and restart the other core, which is the PM master and already
 * awake -- the secure image is simply re-entered as at the end of a cold boot.
 *
 * On a RAM-load platform the image must also be reloaded, not just re-entered:
 * it runs from RAM with its load address equal to its link address, so its
 * initialised data can only be restored by copying the image from flash again
 * (boot_load_secure_image() above). Without that, TF-M restarts with, for
 * instance, an already-consumed partition load list and never runs a single
 * partition.
 *
 * The non-secure side is left untouched: its RAM (including Zephyr's
 * suspend-to-RAM CPU context) is retained, so when TF-M hands over, the
 * non-secure image's reset handler finds the s2ram mark and resumes the
 * suspended context instead of cold-starting.
 *
 * Everything before the hand-over is what the loader's wake path does for the
 * SoC state it owns, kept in the same order.
 */
void BOOT_WakeFromPG(void)
{
#if defined(SOC_AMEBAG2)
	FIH_DECLARE(fih_rc, FIH_FAILURE);

	/* Config Non-Security World Registers Firstly in BOOT_WakeFromPG */
	FIH_CALL(BOOT_Wake_TZCfg, fih_rc);
	if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
		while (1) {
		}
	}

	/*
	 * The secure world takes its exceptions through the ROM vector table, whose
	 * trampolines call the handlers registered with SVCall_irqfunc_set() and
	 * friends. Power-gating resets VTOR to 0 and the ROM's wake path does not
	 * restore it, so put it back before any secure code can take an exception.
	 */
	SCB->VTOR = (uint32_t)RomVectorTable;
	__DSB();
	__ISB();
#elif defined(SOC_AMEBADPLUS)
	BOOT_TRNG_ParaSet();

	/* Config Non-Security World Registers Firstly in BOOT_WakeFromPG */
	BOOT_RAM_TZCfg();

	/* Enable FPU and Secure/Usage/Mem/Bus Fault */
	BOOT_SCBConfig_HP();
#else
#error "no power-gate wake path for this SoC"
#endif

	boot_enter_secure_image();
}


