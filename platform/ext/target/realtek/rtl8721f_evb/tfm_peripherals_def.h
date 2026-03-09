/*
 * Copyright (c) 2018-2022, Arm Limited. All rights reserved.
 * Copyright (c) 2020, Cypress Semiconductor Corporation. All rights reserved.
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef __TFM_PERIPHERALS_DEF_H__
#define __TFM_PERIPHERALS_DEF_H__

#include "cmsis.h"

#ifdef __cplusplus
extern "C" {
#endif

struct platform_data_t;

extern struct platform_data_t tfm_peripheral_std_uart;

#define TFM_PERIPHERAL_STD_UART  (&tfm_peripheral_std_uart)

#ifdef PSA_API_TEST_IPC
extern struct platform_data_t tfm_peripheral_FF_TEST_UART_REGION;
extern struct platform_data_t tfm_peripheral_FF_TEST_WATCHDOG_REGION;
extern struct platform_data_t tfm_peripheral_FF_TEST_NVMEM_REGION;
extern struct platform_data_t tfm_peripheral_FF_TEST_SERVER_PARTITION_MMIO;
extern struct platform_data_t tfm_peripheral_FF_TEST_DRIVER_PARTITION_MMIO;
#define FF_TEST_UART_REGION           (&tfm_peripheral_FF_TEST_UART_REGION)
#define FF_TEST_WATCHDOG_REGION       (&tfm_peripheral_FF_TEST_WATCHDOG_REGION)
#define FF_TEST_NVMEM_REGION          (&tfm_peripheral_FF_TEST_NVMEM_REGION)
#define FF_TEST_SERVER_PARTITION_MMIO (&tfm_peripheral_FF_TEST_SERVER_PARTITION_MMIO)
#define FF_TEST_DRIVER_PARTITION_MMIO (&tfm_peripheral_FF_TEST_DRIVER_PARTITION_MMIO)
#endif /* PSA_API_TEST_IPC */

#ifdef __cplusplus
}
#endif

#endif /* __TFM_PERIPHERALS_DEF_H__ */
