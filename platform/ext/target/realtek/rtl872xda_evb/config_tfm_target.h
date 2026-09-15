/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef __CONFIG_TFM_TARGET_H__
#define __CONFIG_TFM_TARGET_H__

/* Take the PSA random numbers from the on-chip TRNG instead of from an entropy
 * seed in ITS. The two are mutually exclusive, see
 * secure_fw/partitions/crypto/config_crypto_check.h. */
#define CRYPTO_EXT_RNG                    1
#define CRYPTO_NV_SEED                    0

#endif /* __CONFIG_TFM_TARGET_H__ */
