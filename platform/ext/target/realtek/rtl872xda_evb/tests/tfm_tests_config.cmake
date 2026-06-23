#-------------------------------------------------------------------------------
# Copyright (c) 2026, Realtek Semiconductor Corp.
#
# SPDX-License-Identifier: BSD-3-Clause
#
#-------------------------------------------------------------------------------

# Make FLIH IRQ test as the default IRQ test on AN521
set(TEST_NS_SLIH_IRQ                  OFF   CACHE BOOL    "Whether to build NS regression Second-Level Interrupt Handling tests")

set(PLATFORM_SLIH_IRQ_TEST_SUPPORT    OFF)
set(PLATFORM_FLIH_IRQ_TEST_SUPPORT    OFF)
