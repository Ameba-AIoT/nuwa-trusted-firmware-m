/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef AMEBA_PM_RESUME_H
#define AMEBA_PM_RESUME_H

/*
 * Whether this build carries the secure world across a power-gate instead of
 * rebuilding it. Both SoCs are re-entered through the bootloader's
 * BOOT_WakeFromPG(), whether they execute in place or from RAM.
 */
#if defined(SOC_AMEBADPLUS) || defined(SOC_AMEBAG2)
#define AMEBA_PM_CORE_RESUME 1
#endif

/*
 * A power-gate keeps SRAM and loses the core, so everything the SPM holds is
 * still there but has nothing to run on. These two put the core back around the
 * sleep.
 *
 * backup() runs as the last act of the secure service the sleep requests; it also
 * disables the D-cache, so what follows reaches SRAM rather than a cache that
 * does not survive. refill() runs first thing on the wake path and enters the
 * non-secure world rather than returning.
 */

void ameba_pm_core_backup(void);

void ameba_pm_core_refill(void);

#endif /* AMEBA_PM_RESUME_H */
