/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Startup code for RTL8721F (AmebaG2) TF-M platform
 */

#include "tfm_hal_device_header.h"

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

    /* __NVIC_SetVector(SVCall_IRQn, (uint32_t)SVC_Handler); */
    /* __NVIC_SetVector(PendSV_IRQn, (uint32_t)PendSV_Handler); */
	SVCall_irqfunc_set(SVC_Handler);
	PendSV_irqfunc_set(PendSV_Handler);

    SystemInit();                             /* CMSIS System Initialization */
    __PROGRAM_START();                        /* Enter PreMain (C library entry point) */
}

/* This is a stub to make the linker happy */
void __Vectors(){}
void SVC_Handler(void){}
void PendSV_Handler(void){}
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
