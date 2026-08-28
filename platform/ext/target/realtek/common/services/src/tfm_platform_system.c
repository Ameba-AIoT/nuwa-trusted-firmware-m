/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tfm_platform_system.h"
#include "tfm_hal_platform.h"
#include "cmsis.h"

#if defined(SOC_AMEBAG2)
#include <ameba_soc.h>

#include "ameba_pmc_tz_ioctl.h"
#endif

void tfm_platform_hal_system_reset(void)
{
    /* Platform-specific reset implementation */
    NVIC_SystemReset();
}

#if defined(SOC_AMEBAG2)
/*
 * Hand an IP over to the non-secure zone for the duration of a power-gated sleep,
 * or take it back afterwards. See ameba_pmc_tz_ioctl.h for why the non-secure side
 * cannot do this itself and why only a fixed set of IPs is permitted.
 */
static enum tfm_platform_err_t ameba_ppc_permission(psa_invec *in_vec)
{
    const struct ameba_pmc_tz_ppc_request *req;
    uint32_t ctrl;

    if ((in_vec == NULL) || (in_vec->base == NULL) ||
        (in_vec->len != sizeof(struct ameba_pmc_tz_ppc_request))) {
        return TFM_PLATFORM_ERR_INVALID_PARAM;
    }

    req = (const struct ameba_pmc_tz_ppc_request *)in_vec->base;

    /* Reject any IP the non-secure world has no business asking for. */
    if ((req->ip_mask == 0U) || ((req->ip_mask & ~AMEBA_PMC_TZ_PPC_ALLOWED) != 0U)) {
        return TFM_PLATFORM_ERR_INVALID_PARAM;
    }

    ctrl = HAL_READ32(SYSTEM_CTRL_BASE_S, REG_LSYS_SEC_PPC_CTRL);
    if (req->release != 0U) {
        ctrl |= req->ip_mask;
    } else {
        ctrl &= ~req->ip_mask;
    }
    HAL_WRITE32(SYSTEM_CTRL_BASE_S, REG_LSYS_SEC_PPC_CTRL, ctrl);

    return TFM_PLATFORM_ERR_SUCCESS;
}
#endif /* SOC_AMEBAG2 */

enum tfm_platform_err_t tfm_platform_hal_ioctl(tfm_platform_ioctl_req_t request,
                                                psa_invec *in_vec,
                                                psa_outvec *out_vec)
{
    (void)out_vec;

#if defined(SOC_AMEBAG2)
    if (request == AMEBA_PMC_TZ_IOCTL_PPC_PERMISSION) {
        return ameba_ppc_permission(in_vec);
    }
#else
    (void)in_vec;
#endif

    (void)request;

    return TFM_PLATFORM_ERR_NOT_SUPPORTED;
}
