/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Realtek AES-GCM / HMAC-SHA256 hardware adapter: a platform-agnostic interface
 * over the per-SoC ROM crypto APIs (amebag2 crypto_*, amebadplus rtl_crypto_*).
 *
 * The key is always supplied by the caller as a software key (amebag2 loads it
 * into the secure KM slot), see ameba_its_key.h. The on-chip IPSEC/OTP hardware
 * key is deliberately not offered: it is shared by every device flashed with the
 * same OTP image.
 *
 * The engines are DMA driven, so destination buffers must be cache-line aligned
 * and own their trailing line: the driver invalidates over the range and would
 * otherwise discard a neighbour's dirty data. Sources are only cleaned and need
 * no more than the 4-byte alignment the ROM APIs ask for.
 *
 * The payload may be transformed in place (\p ct == \p pt): the GCM payload is a
 * single DMA pass with the GHASH accumulated in hardware, so the input is never
 * re-read after being overwritten. Partial overlap is NOT supported.
 */

#ifndef AMEBA_HW_GCM_H
#define AMEBA_HW_GCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief AES-GCM authenticated encryption.
 *
 * \param[in]  key      Software key (must not be NULL).
 * \param[in]  key_len  Length of \p key in bytes (16, 24 or 32).
 * \param[in]  iv       Initialization vector / nonce (12 bytes for GCM).
 * \param[in]  iv_len   Length of \p iv in bytes.
 * \param[in]  aad      Additional authenticated data (may be NULL if aad_len==0).
 * \param[in]  aad_len  Length of \p aad in bytes.
 * \param[in]  pt       Plaintext input.
 * \param[in]  len      Length of \p pt / \p ct in bytes.
 * \param[out] ct       Ciphertext output (\p len bytes). May be \p pt (in place).
 * \param[out] tag      Authentication tag output.
 * \param[in]  tag_len  Length of \p tag in bytes (typically 16).
 *
 * \return 0 on success, negative value on error.
 */
int ameba_hw_gcm_encrypt(const uint8_t *key, size_t key_len,
                         const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *pt, size_t len,
                         uint8_t *ct,
                         uint8_t *tag, size_t tag_len);

/**
 * \brief AES-GCM authenticated decryption.
 *
 * The authentication tag is verified. A mismatch returns a negative value.
 *
 * \param[in]  key      Software key (must not be NULL).
 * \param[in]  key_len  Length of \p key in bytes (16, 24 or 32).
 * \param[in]  iv       Initialization vector / nonce (12 bytes for GCM).
 * \param[in]  iv_len   Length of \p iv in bytes.
 * \param[in]  aad      Additional authenticated data (may be NULL if aad_len==0).
 * \param[in]  aad_len  Length of \p aad in bytes.
 * \param[in]  ct       Ciphertext input.
 * \param[in]  len      Length of \p ct / \p pt in bytes.
 * \param[in]  tag      Expected authentication tag.
 * \param[in]  tag_len  Length of \p tag in bytes (typically 16).
 * \param[out] pt       Plaintext output (\p len bytes). May be \p ct (in place).
 *                      On a tag mismatch it holds unauthenticated data; the
 *                      caller must discard it.
 *
 * \return 0 on success (tag verified), negative value on error / tag mismatch.
 */
int ameba_hw_gcm_decrypt(const uint8_t *key, size_t key_len,
                         const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *ct, size_t len,
                         const uint8_t *tag, size_t tag_len,
                         uint8_t *pt);


/** Output size of ameba_hw_hmac_sha256(), in bytes. */
#define AMEBA_HW_SHA256_LEN  32u

/**
 * \brief HMAC-SHA256 using the on-chip SHA engine.
 *
 * Used by the ITS key derivation (ameba_its_key.c), which runs before the
 * Crypto service exists and therefore cannot use PSA crypto. Sized for that
 * single caller: longer inputs may be rejected rather than bounced.
 *
 * \param[in]  key      HMAC key (must not be NULL), 16/24/32 bytes.
 * \param[in]  key_len  Length of \p key in bytes (<= 32).
 * \param[in]  msg      Message to authenticate (must not be NULL).
 * \param[in]  msg_len  Length of \p msg in bytes (<= 32).
 * \param[out] digest   Output, AMEBA_HW_SHA256_LEN bytes.
 *
 * \return 0 on success, negative value on error.
 */
int ameba_hw_hmac_sha256(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t digest[AMEBA_HW_SHA256_LEN]);

/**
 * \brief Fill a buffer with hardware TRNG random bytes.
 *
 * Provided by the per-SoC adapter so the shared HAL does not depend on
 * platform headers.
 *
 * \param[out] buf  Destination buffer.
 * \param[in]  len  Number of bytes to fill.
 *
 * \return 0 on success, negative value on error.
 */
int ameba_hw_random(uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* AMEBA_HW_GCM_H */
