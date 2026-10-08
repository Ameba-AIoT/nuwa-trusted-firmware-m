/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Startup code for RTL8721F (AmebaG2) TF-M platform
 */

#include "tfm_hal_device_header.h"
#include "region.h"
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
#include "ameba_pm_resume.h"
#endif
#include <string.h>

/*----------------------------------------------------------------------------
  External References
 *----------------------------------------------------------------------------*/
extern uint32_t __INITIAL_SP;
extern uint32_t __STACK_LIMIT;
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
extern uint64_t __STACK_SEAL;
#endif

extern __NO_RETURN void __PROGRAM_START(void);
extern void __libc_init_array(void);
extern int main(void);

/* DerivedKey_Bkup: preserved across BSS clear; copied from ROM DerivedKey before
 * __PROGRAM_START zeros .TFM_BSS. Placed in .noinit (NOLOAD, not in zero table). */
__attribute__((section(".noinit"))) u8 DerivedKey_Bkup[16];

/*
 * True when the bootloader re-entered this image after a power-gate that kept
 * SRAM. lib_pmc.a sets LSYS_BIT_BOOT_WAKE_FROM_PS_HS before the AP goes down and
 * clears it only once the non-secure world has resumed, well after this runs.
 * Nothing else sets it, so cold boot, watchdog reset and deep-sleep wake all take
 * the full path -- as they must, the non-secure world starting from scratch there.
 *
 * Read through the non-secure alias: REG_LSYS_BOOT_CFG is marked DD_SEC: bpc_cpu0
 * and BPC_CPU0 belongs to the non-secure zone for the sleep's duration.
 */
static bool boot_is_power_gate_wake(void)
{
	return (HAL_READ32(SYSTEM_CTRL_BASE, REG_LSYS_BOOT_CFG) &
		LSYS_BIT_BOOT_WAKE_FROM_PS_HS) != 0U;
}

/*----------------------------------------------------------------------------
  Internal References
 *----------------------------------------------------------------------------*/
__NO_RETURN void Reset_Handler (void);

#ifdef BL2
typedef void (*VECTOR_TABLE_Type)(void);
const VECTOR_TABLE_Type __VECTOR_TABLE[] __VECTOR_TABLE_ATTRIBUTE = {
  (VECTOR_TABLE_Type)(&__INITIAL_SP),     /*      Initial Stack Pointer */
/* Exceptions */
    Reset_Handler,
    /*REVIEW: need more? */
};
#else
RAM_START_FUNCTION TFMEntryFun __VECTOR_TABLE_ATTRIBUTE = {
    Reset_Handler,
    NULL,
    (uint32_t)0
};
#endif

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 1U)
HAL_VECTOR_FUN RamVectorTable[95] __attribute__((aligned(512), section(".ramvectortable")));
#endif
/*----------------------------------------------------------------------------
  Reset Handler called on controller reset
 *----------------------------------------------------------------------------*/

REGION_DECLARE(Image$$, TFM_UNPRIV_CODE_LOADADDR, $$Base);
REGION_DECLARE(Image$$, TFM_UNPRIV_CODE_START, $$Base);
REGION_DECLARE(Image$$, TFM_UNPRIV_CODE_END, $$Limit);

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

    if (boot_is_power_gate_wake()) {
        /*
         * A power-gate kept SRAM, so the secure world is all still there: the
         * partition and service lists, the connection pool, the threads and their
         * stacks. What it lost is the core, and the sleep recorded that.
         *
         * Taken before any of the boot work below, which prepares a core this path
         * configures from the record instead. Does not return; it enters the
         * non-secure world.
         */
        ameba_pm_core_refill();
    }

    u32 size  = (uint32_t)&REGION_NAME(Image$$, TFM_UNPRIV_CODE_END, $$Limit) - (uint32_t)&REGION_NAME(Image$$, TFM_UNPRIV_CODE_START, $$Base);
    u32 *dst = (uint32_t *)&REGION_NAME(Image$$, TFM_UNPRIV_CODE_START, $$Base);
    u32 *src = (uint32_t *)&REGION_NAME(Image$$, TFM_UNPRIV_CODE_LOADADDR, $$Base);
    for (u32 idx = 0; idx < size / 4; idx++) {
        dst[idx] = src[idx];
    }
    DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);

    /* __NVIC_SetVector(SVCall_IRQn, (uint32_t)SVC_Handler); */
    /* __NVIC_SetVector(PendSV_IRQn, (uint32_t)PendSV_Handler); */
	SVCall_irqfunc_set(SVC_Handler);
	PendSV_irqfunc_set(PendSV_Handler);
	/* Copy DerivedKey only on cold boot / deep-sleep wakeup; on warm reset the
	 * ROM clears its BSS, so keep the value from the last cold boot. */
	if ((BOOT_Reason() == 0) || (BOOT_Reason() == AON_BIT_RSTF_DSLP)) {
		extern u8 DerivedKey[16];
		memcpy(DerivedKey_Bkup, DerivedKey, sizeof(DerivedKey_Bkup));
	}
#else
    extern void SysTick_Handler (void);

    SCB->VTOR = (u32)RomVectorTable;

    uint32_t *pSrc  = (uint32_t *)RomVectorTable;
    uint32_t *pDest = (uint32_t *)RamVectorTable;
    uint32_t count  = sizeof(RamVectorTable) / sizeof(uint32_t);

    for (uint32_t i = 0; i < count; i++) {
        pDest[i] = pSrc[i];
    }
    RamVectorTable[0]  = (HAL_VECTOR_FUN)MSP_RAM_HP_NS;
    RamVectorTable[11] = SVC_Handler;
    RamVectorTable[14] = PendSV_Handler;
    RamVectorTable[15] = SysTick_Handler;

    __DSB();
    SCB->VTOR = (uint32_t)RamVectorTable;
    __DSB();
    __ISB();
#endif

    SystemInit();                             /* CMSIS System Initialization */

    __PROGRAM_START();                        /* Enter PreMain (C library entry point) */
}

/*
 * Reset_Handler
 *     ├─► Disable interrupts (cpsid i)
 *     ├─► Set PSP, MSPLIM, PSPLIM
 *     ├─► Set SVC/PendSV handlers
 *     ├─► Data Copy Loop (__copy_table)
 *     ├─► BSS Zero Loop (__zero_table)
 *     │
 *     └─► ___start_veneer → _mainCRTStartup
 *             ├─► Set stack pointer
 *             ├─► _stack_init()
 *             ├─► memset BSS (__bss_start__-__bss_end__)
 *             ├─► __libc_init_array() (C++ constructors)
 *             ├─► atexit() registration
 *             │
 *             └─► __main_veneer → main
 *                     ├─► tfm_hal_set_up_static_boundaries()
 *                     ├─► tfm_hal_platform_init()
 *                     ├─► tfm_plat_otp_init()
 *                     ├─► Provisioning checks
 *                     ├─► tfm_arch_config_extensions()
 *                     ├─► tfm_core_validate_boot_data()
 *                     ├─► tfm_arch_set_secure_exception_priorities()
 *                     │
 *                     └─► svc #0 → SVC_Handler → TFM Scheduler
 */
