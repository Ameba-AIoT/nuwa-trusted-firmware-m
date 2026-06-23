/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * BL2-compatible NS ARM vector table for RTL8721F (AmebaG2).
 *
 * In BL2 mode img1 loads the signed NS binary to XIP NS_AP_LOGIC_BASE.
 * TF-M Secure side reads:
 *   tfm_hal_get_ns_MSP()         <- NS_AP_LOGIC_BASE + BL2_HEADER_SIZE + 0
 *   tfm_hal_get_ns_entry_point() <- NS_AP_LOGIC_BASE + BL2_HEADER_SIZE + 4
 *
 * This section is placed first in .text at NS_CODE_START (= NS_AP_LOGIC_BASE +
 * BL2_HEADER_SIZE) so both reads return the correct initial SP and Reset_Handler.
 */

    .section ".vt_bl2", "a"
    .global tfm_ns_bl2_vt
    .type   tfm_ns_bl2_vt, %object
    .size   tfm_ns_bl2_vt, 8
tfm_ns_bl2_vt:
    .word   __StackTop          /* +0: initial SP */
    .word   Reset_Handler + 1   /* +4: Reset_Handler with Thumb bit */
