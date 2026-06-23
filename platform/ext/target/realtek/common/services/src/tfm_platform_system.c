/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tfm_platform_system.h"
#include "tfm_hal_platform.h"
#include "cmsis.h"

void tfm_platform_hal_system_reset(void)
{
    /* Platform-specific reset implementation */
    NVIC_SystemReset();
}

enum tfm_platform_err_t tfm_platform_hal_ioctl(tfm_platform_ioctl_req_t request,
                                                psa_invec *in_vec,
                                                psa_outvec *out_vec)
{
    (void)request;
    (void)in_vec;
    (void)out_vec;

    return TFM_PLATFORM_ERR_NOT_SUPPORTED;
}
