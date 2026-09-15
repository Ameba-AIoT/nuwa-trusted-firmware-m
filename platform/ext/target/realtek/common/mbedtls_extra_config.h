/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef __MBEDTLS_EXTRA_CONFIG_H__
#define __MBEDTLS_EXTRA_CONFIG_H__

/* Appended to the TF-M Mbed TLS config as MBEDTLS_USER_CONFIG_FILE, so these
 * win over the defaults. CRYPTO_EXT_RNG does not imply either of them. */

/* Random numbers come from mbedtls_psa_external_get_random(). */
#undef MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG
#define MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG 1

/* No persistent entropy seed then, and no ITS-backed seed source. */
#undef MBEDTLS_ENTROPY_NV_SEED

#endif /* __MBEDTLS_EXTRA_CONFIG_H__ */
