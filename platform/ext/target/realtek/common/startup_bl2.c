/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Startup code for RTL8721F (AmebaG2) TF-M platform
 */

#include "boot_prepare.h"
#include "tfm_hal_device_header.h"
#include "boot_hal.h"
#include "tfm_plat_defs.h"
#include "fih.h"
#include "bootutil/bootutil.h"
#include <sysflash/sysflash.h>

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

/* This is a stub to make the linker happy */
// void __Vectors(){}

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

int32_t boot_platform_post_init(void)
{
	return 0;
}

int boot_platform_pre_load(uint32_t image_id)
{
	return 0;
}

bool boot_platform_should_load_image(uint32_t image_id)
{
	return true;
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
