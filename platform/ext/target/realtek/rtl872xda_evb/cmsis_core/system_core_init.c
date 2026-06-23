/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "system_core_init.h"
#include "cmsis_cpu.h"

/* System Core Clock Frequency */
uint32_t SystemCoreClock = 200000000UL; /* 200 MHz default */

void SystemInit(void)
{
    /* System initialization is handled by startup code */
    /* This function can be extended for platform-specific init */
#if defined (__FPU_USED) && (__FPU_USED == 1U)
    SCB->CPACR |= ((3U << 10U*2U) |           /* enable CP10 Full Access */
                   (3U << 11U*2U)  );         /* enable CP11 Full Access */
#endif

#ifdef UNALIGNED_SUPPORT_DISABLE
    SCB->CCR |= SCB_CCR_UNALIGN_TRP_Msk;
#endif
}

uint32_t SystemGetTick(void)
{
    return SysTick->VAL;
}
