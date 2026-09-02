/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Startup code for RTL8721Dx (Amebadplus) TF-M platform
 */

#include "tfm_hal_device_header.h"
#include "region.h"
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

/* DerivedKey_Bkup: preserved across BSS clear; copied from ROM DerivedKey before
 * __PROGRAM_START zeros .TFM_BSS. Placed in .noinit (NOLOAD, not in zero table). */
__attribute__((section(".noinit"))) u8 DerivedKey_Bkup[16];

/*----------------------------------------------------------------------------
  Internal References
 *----------------------------------------------------------------------------*/
__NO_RETURN void Reset_Handler (void);

typedef void (*VECTOR_TABLE_Type)(void);
#ifdef BL2
/* BL2 needs a proper ARM Cortex-M vector table:
 * [0] = Initial MSP value
 * [1] = Reset_Handler address
 * bl2_main.c reads vt->reset from index [1]; without this,
 * TFMEntryFun[1]=NULL causes vt->reset=0 and boot fails. */
const VECTOR_TABLE_Type __VECTOR_TABLE[] __VECTOR_TABLE_ATTRIBUTE = {
    (VECTOR_TABLE_Type)(&__INITIAL_SP),
    Reset_Handler,
};
#else
RAM_START_FUNCTION TFMEntryFun __VECTOR_TABLE_ATTRIBUTE = {
    Reset_Handler,
    NULL,
    (uint32_t)0
};
/* Minimal [MSP, Reset] flash vector table at the NS image start (section
 * .ns_boot_vectors). The secure->NS jump reads MSP/entry from here
 * (NS_AP_LOGIC_BASE + 0x400); without it the jump reads code bytes as MSP and
 * double-faults. The amebadplus NS otherwise keeps runtime vectors in RAM
 * (NewVectorTable), which BL2 never populates. */
const VECTOR_TABLE_Type __ns_boot_vector_table[]
    __attribute__((section(".ns_boot_vectors"), used)) = {
    (VECTOR_TABLE_Type)(&__INITIAL_SP),
    Reset_Handler,
};
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
#endif

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    u32 size  = (uint32_t)&REGION_NAME(Image$$, TFM_UNPRIV_CODE_END, $$Limit) - (uint32_t)&REGION_NAME(Image$$, TFM_UNPRIV_CODE_START, $$Base);
    u32 *dst = (uint32_t *)&REGION_NAME(Image$$, TFM_UNPRIV_CODE_START, $$Base);
    u32 *src = (uint32_t *)&REGION_NAME(Image$$, TFM_UNPRIV_CODE_LOADADDR, $$Base);
    for (u32 idx = 0; idx < size / 4; idx++) {
        dst[idx] = src[idx];
    }
    DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);

    /* __NVIC_SetVector(SVCall_IRQn, (uint32_t)SVC_Handler); */
    /* __NVIC_SetVector(PendSV_IRQn, (uint32_t)PendSV_Handler); */
	NewVectorTable[11] = (HAL_VECTOR_FUN)SVC_Handler;
	NewVectorTable[14] = (HAL_VECTOR_FUN)PendSV_Handler;

	/* Ensure VTOR_S points to the ROM-managed secure vector table at
	 * NewVectorTable (0x30007000). BL2 (loaded from RSIP-mapped flash) may
	 * have left VTOR_S elsewhere, in which case the SVC_Handler/PendSV_Handler
	 * we just wrote into NewVectorTable would never get invoked. */
	__DSB();
	SCB->VTOR = (uint32_t)NewVectorTable;
	__DSB();
	__ISB();

	/* Reset_Handler started with cpsid i (PRIMASK_S=1). With PRIMASK set,
	 * SVC instructions escalate to HardFault instead of invoking SVCall.
	 * TFM uses SVC for BACKEND_SPM_INIT and other syscalls, so re-enable
	 * IRQs before main runs. */
	__enable_irq();
	/* Copy DerivedKey only on cold boot / deep-sleep wakeup; on warm reset the
	 * ROM clears its BSS, so keep the value from the last cold boot. */
	if ((BOOT_Reason() == 0) || (BOOT_Reason() == AON_BIT_RSTF_DSLP)) {
		extern u8 DerivedKey[16];
		memcpy(DerivedKey_Bkup, DerivedKey, sizeof(DerivedKey_Bkup));
	}
#else
    extern void SysTick_Handler (void);

    NewVectorTable[0]  = (HAL_VECTOR_FUN)MSP_RAM_HP_NS;
    NewVectorTable[11] = SVC_Handler;
    NewVectorTable[14] = PendSV_Handler;
    NewVectorTable[15] = SysTick_Handler;

    __DSB();
    SCB->VTOR = (uint32_t)NewVectorTable;
    __DSB();
    __ISB();

    /* Clear NVIC enable/pending inherited from Secure: NewVectorTable only holds
     * the system handlers, so a stray NS IRQ after unmask would vector to an
     * unset entry and HardFault before main. */
    for (uint32_t i = 0U; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }
    /* Stop/de-pend SysTick too; osKernelStart reconfigures it. */
    SysTick->CTRL = 0UL;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    __DSB();
    __ISB();

    /* Unmask PRIMASK_NS (Secure masked it for the handoff) now VTOR is set. */
    __enable_irq();
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
