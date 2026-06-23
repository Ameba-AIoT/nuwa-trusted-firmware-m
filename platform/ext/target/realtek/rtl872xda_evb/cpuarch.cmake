#-------------------------------------------------------------------------------
# Copyright (c) 2023, Arm Limited. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
#-------------------------------------------------------------------------------

# In the new split build this file defines a platform specific parameters
# like mcpu core, arch etc and to be included by toolchain files.

set(TFM_SYSTEM_PROCESSOR cortex-m55)
set(TFM_SYSTEM_ARCHITECTURE armv8.1-m.main)
set(TFM_SYSTEM_DSP ON)
set(CONFIG_TFM_FLOAT_ABI "hard")
set(CONFIG_TFM_FP_ARCH "fpv5-sp-d16")
set(CONFIG_TFM_FP_ARCH_ASM "FPv5_SP_D16")
# amebadplus precompiled libs use hard-float VFP ABI; BL2 must match
set(CONFIG_TFM_BL2_FLOAT_ABI "hard")
