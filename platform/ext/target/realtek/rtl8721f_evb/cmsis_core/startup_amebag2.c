/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Startup code for RTL8721F (AmebaG2) TF-M platform
 */

#include "tfm_hal_device_header.h"
#include "region.h"

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

RAM_START_FUNCTION TFMEntryFun __VECTOR_TABLE_ATTRIBUTE = {
    Reset_Handler,
    NULL,
    (uint32_t)0
};

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 1U)
HAL_VECTOR_FUN RamVectorTable[95] __attribute__((aligned(512), section(".noinit")));
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
	SVCall_irqfunc_set(SVC_Handler);
	PendSV_irqfunc_set(PendSV_Handler);
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
