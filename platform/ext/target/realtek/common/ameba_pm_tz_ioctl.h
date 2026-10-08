/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef AMEBA_PM_TZ_IOCTL_H
#define AMEBA_PM_TZ_IOCTL_H

#include <stdint.h>

/*
 * Contract between the non-secure PM code and the TF-M platform service: the
 * things a sleep needs done that only the secure world can do, one request ID
 * each. Installed to the non-secure interface directory so both sides compile
 * against this copy; include <ameba_soc.h> first, the AmebaG2 masks below being
 * LSYS_BIT_BPC_* from sysreg_lsys.h.
 */

/* Both builds compile this header and spell the SoC differently. */
#if defined(SOC_AMEBAG2) || defined(CONFIG_SOC_SERIES_AMEBAG2)
#define AMEBA_PM_TZ_SOC_AMEBAG2 1
#endif

/* The request IDs this platform's tfm_platform_hal_ioctl() serves. */
enum ameba_pm_tz_ioctl_req_t {
	AMEBA_PMC_TZ_IOCTL_PPC_PERMISSION,
	AMEBA_PMC_TZ_IOCTL_BPC_PERMISSION,
	AMEBA_PM_TZ_IOCTL_SUSPEND,
};

/*
 * AMEBA_PM_TZ_IOCTL_SUSPEND -- a power-gate is imminent, and the secure world is
 * carried across it rather than rebuilt. No payload.
 *
 * It records the core state the power-gate clears, which is only described by the
 * registers themselves, and disables the D-cache so that what it holds of the
 * secure world's own RAM reaches SRAM. Nothing else on the sleep path does the
 * latter: vPortSystemPowerOff() cleans its own sleep_param and then disables the
 * cache, so a line the secure world dirtied and did not touch again goes down
 * with it -- one stale word rather than a crash, so it has to be prevented rather
 * than detected. Both concern the secure world's own core and RAM, whose extent
 * is known only there.
 *
 * Necessarily the sleep's last secure call: anything written afterwards would be
 * back in a cache that does not survive.
 */

#if defined(AMEBA_PM_TZ_SOC_AMEBAG2)
/*
 * AMEBA_PMC_TZ_IOCTL_{PPC,BPC}_PERMISSION -- AmebaG2 only. Hands peripherals over
 * to the non-secure zone across a sleep and takes them back afterwards.
 *
 * A power-gated AP is powered back up by the non-secure NP/LP side, but
 * BOOT_SecureChip_PeriCfg() leaves PSRAM / SPIC / CPU0 secure-only, so without
 * the hand-over nobody can wake it: both cores stay dark until the IWDG resets
 * the chip. Every field of REG_LSYS_SEC_PPC_CTRL and REG_LSYS_SEC_BPC_CTRL is
 * marked DD_SEC: S, and the SOCPS_*PermissionEntry() veneers that would cross
 * worlds do not exist under TF-M, so the writes go through the platform service.
 *
 * Masks and timing mirror the hal's AP-side vPortSystemPowerOff() and
 * SOCPS_SleepCG() (lib/pmc/ameba_pmc_km4tz_lib.c), reproducing its steady state
 * exactly: PPC_CTRL = 0xff00f0ff released, 0xcf00f0ff reclaimed.
 */

/* Handed to the non-secure zone before a power-gated sleep. */
#define AMEBA_PMC_TZ_PPC_SLEEP_RELEASE                                                             \
	(LSYS_BIT_BPC_PSRAM | LSYS_BIT_BPC_SPIC | LSYS_BIT_BPC_CPU0 | LSYS_BIT_BPC_PMC)

/* Taken back once the AP is running again; CPU0 and PMC stay non-secure. */
#define AMEBA_PMC_TZ_PPC_WAKE_RECLAIM (LSYS_BIT_BPC_PSRAM | LSYS_BIT_BPC_SPIC)

/*
 * Everything the secure side is willing to act on, enforced there so a
 * compromised non-secure world cannot grant itself unrelated IPs. The hal's NSC
 * veneer does not check this.
 */
#define AMEBA_PMC_TZ_PPC_ALLOWED AMEBA_PMC_TZ_PPC_SLEEP_RELEASE

/*
 * REG_LSYS_SEC_BPC_CTRL, handed over alongside the peripheral permissions. The
 * power-gate group is larger: a gated AP cannot drive the crypto engines' clock
 * and power itself, so the NP -- non-secure by construction here -- has to, which
 * clock-gating does not need. Reclaimed as soon as the AP is back, before any
 * secure service can be called again.
 */
#define AMEBA_PMC_TZ_BPC_CLOCK_GATE                                                                \
	(LSYS_BIT_BPC_ATIM | LSYS_BIT_BPC_ADC | LSYS_BIT_BPC_GPIO | LSYS_BIT_BPC_TRNG)
#define AMEBA_PMC_TZ_BPC_POWER_GATE                                                                \
	(AMEBA_PMC_TZ_BPC_CLOCK_GATE | LSYS_BIT_BPC_PKE | LSYS_BIT_BPC_CRYPTO)
#define AMEBA_PMC_TZ_BPC_ALLOWED AMEBA_PMC_TZ_BPC_POWER_GATE

/*
 * psa_invec payload for both permission request IDs; the ID says which register
 * is meant. release != 0 sets the mask bits (IP controlled by the non-secure
 * zone), release == 0 clears them (secure-only).
 */
struct ameba_pmc_tz_ppc_request {
	uint32_t ip_mask;
	uint32_t release;
};
#endif /* AMEBA_PM_TZ_SOC_AMEBAG2 */

#endif /* AMEBA_PM_TZ_IOCTL_H */
