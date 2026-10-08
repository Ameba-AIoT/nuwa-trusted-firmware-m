/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tfm_platform_system.h"
#include "tfm_hal_platform.h"
#include "cmsis.h"
#include "region_defs.h"

#include <ameba_soc.h>

#include "ameba_pm_tz_ioctl.h"


#include "ameba_pm_resume.h"

void tfm_platform_hal_system_reset(void)
{
    /* Platform-specific reset implementation */
    NVIC_SystemReset();
}

#if defined(SOC_AMEBAG2)
/*
 * Hand an IP over to the non-secure zone for the duration of a sleep, or take it
 * back afterwards. Serves both ownership registers; the request ID picks which
 * one, and each brings its own whitelist so neither has to widen to cover the
 * other's IPs. See ameba_pm_tz_ioctl.h for why the non-secure side cannot do this
 * itself and why only a fixed set of IPs is permitted.
 */
static enum tfm_platform_err_t ameba_permission(psa_invec *in_vec, uint32_t reg,
                                                uint32_t allowed)
{
    const struct ameba_pmc_tz_ppc_request *req;
    uint32_t ctrl;

    if ((in_vec == NULL) || (in_vec->base == NULL) ||
        (in_vec->len != sizeof(struct ameba_pmc_tz_ppc_request))) {
        return TFM_PLATFORM_ERR_INVALID_PARAM;
    }

    req = (const struct ameba_pmc_tz_ppc_request *)in_vec->base;

    /* Reject any IP the non-secure world has no business asking for. */
    if ((req->ip_mask == 0U) || ((req->ip_mask & ~allowed) != 0U)) {
        return TFM_PLATFORM_ERR_INVALID_PARAM;
    }

    ctrl = HAL_READ32(SYSTEM_CTRL_BASE_S, reg);
    if (req->release != 0U) {
        ctrl |= req->ip_mask;
    } else {
        ctrl &= ~req->ip_mask;
    }
    HAL_WRITE32(SYSTEM_CTRL_BASE_S, reg, ctrl);

    return TFM_PLATFORM_ERR_SUCCESS;
}
#endif /* SOC_AMEBAG2 */

#if defined(AMEBA_PM_CORE_RESUME)
/*
 * AMEBA_PM_TZ_IOCTL_SUSPEND: the sleep telling the secure world it is about to lose
 * the core. Records what has to come back and stops using the D-cache; see
 * ameba_pm_resume.h.
 *
 * Has to be the last secure call the sleep makes, because the D-cache goes with it.
 */
static enum tfm_platform_err_t ameba_suspend_backup_core(void)
{
    ameba_pm_core_backup();

    return TFM_PLATFORM_ERR_SUCCESS;
}
#endif /* AMEBA_PM_CORE_RESUME */

enum tfm_platform_err_t tfm_platform_hal_ioctl(tfm_platform_ioctl_req_t request,
                                                psa_invec *in_vec,
                                                psa_outvec *out_vec)
{
    (void)out_vec;

#if defined(AMEBA_PM_CORE_RESUME)
    if (request == AMEBA_PM_TZ_IOCTL_SUSPEND) {
        return ameba_suspend_backup_core();
    }
#endif

#if defined(SOC_AMEBAG2)
    if (request == AMEBA_PMC_TZ_IOCTL_PPC_PERMISSION) {
        return ameba_permission(in_vec, REG_LSYS_SEC_PPC_CTRL, AMEBA_PMC_TZ_PPC_ALLOWED);
    }
    if (request == AMEBA_PMC_TZ_IOCTL_BPC_PERMISSION) {
        return ameba_permission(in_vec, REG_LSYS_SEC_BPC_CTRL, AMEBA_PMC_TZ_BPC_ALLOWED);
    }
#else
    (void)in_vec;
#endif

    (void)request;

    return TFM_PLATFORM_ERR_NOT_SUPPORTED;
}
