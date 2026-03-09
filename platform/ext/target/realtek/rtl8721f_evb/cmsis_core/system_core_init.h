/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SYSTEM_CORE_INIT_H__
#define __SYSTEM_CORE_INIT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  \brief   System Initialization
  \details Initializes the system.
 */
void SystemInit(void);

/**
  \brief   Get System Tick
  \details Returns the current system tick value.
 */
uint32_t SystemGetTick(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_CORE_INIT_H__ */
