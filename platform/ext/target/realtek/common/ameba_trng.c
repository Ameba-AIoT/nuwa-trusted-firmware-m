/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

/*
 * Entropy hooks backed by the on-chip TRNG. Only linked into platform_s, which
 * is the sole target carrying an Mbed TLS config.
 */

#include <ameba_soc.h>

#include "mbedtls/entropy.h"
#include "psa/crypto.h"

#if defined(MBEDTLS_ENTROPY_HARDWARE_ALT)
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
	(void)data;

	*olen = 0;
	if (TRNG_get_random_bytes(output, (uint32_t)len) != 0) {
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	}
	*olen = len;
	return 0;
}
#endif /* MBEDTLS_ENTROPY_HARDWARE_ALT */

#if defined(MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG)
psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t *context,
											uint8_t *output, size_t output_size,
											size_t *output_length)
{
	(void)context;

	*output_length = 0;
	if (TRNG_get_random_bytes(output, (uint32_t)output_size) != 0) {
		return PSA_ERROR_INSUFFICIENT_ENTROPY;
	}
	*output_length = output_size;
	return PSA_SUCCESS;
}
#endif /* MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG */
