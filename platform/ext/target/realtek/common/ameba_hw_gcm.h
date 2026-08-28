/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Realtek AES-GCM hardware adapter.
 *
 * This header defines a small, platform-agnostic AES-GCM interface that hides
 * the differences between the per-SoC ROM crypto APIs:
 *   - amebag2   (RTL8721F)  : crypto_gcm_encrypt_and_tag / crypto_gcm_auth_decrypt
 *   - amebadplus(RTL872XDA) : CRYPTO_OTPKey_Init + rtl_crypto_aes_gcm_encrypt/decrypt
 *
 * The key used for the operation is the on-chip IPSEC/OTP hardware key
 * (S_IPSEC_KEY1). The raw key material never enters CPU-addressable memory;
 * the AES engine loads it directly from OTP by key id. Therefore the caller
 * only supplies IV / AAD / data, never a key.
 *
 * Buffers passed to these functions MUST be 32-byte (cache-line) aligned and
 * own their trailing cache line, because the AES engine is DMA driven and the
 * driver performs D-cache clean/invalidate over the supplied ranges. The
 * shared HAL (tfm_hal_its_encryption.c) guarantees this by bouncing through
 * aligned buffers before calling in here.
 */

#ifndef AMEBA_HW_GCM_H
#define AMEBA_HW_GCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief AES-256-GCM authenticated encryption using the on-chip OTP key.
 *
 * \param[in]  iv       Initialization vector / nonce (12 bytes for GCM).
 * \param[in]  iv_len   Length of \p iv in bytes.
 * \param[in]  aad      Additional authenticated data (may be NULL if aad_len==0).
 * \param[in]  aad_len  Length of \p aad in bytes.
 * \param[in]  pt       Plaintext input.
 * \param[in]  len      Length of \p pt / \p ct in bytes.
 * \param[out] ct       Ciphertext output (\p len bytes).
 * \param[out] tag      Authentication tag output.
 * \param[in]  tag_len  Length of \p tag in bytes (typically 16).
 *
 * \return 0 on success, negative value on error.
 */
int ameba_hw_gcm_encrypt(const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *pt, size_t len,
                         uint8_t *ct,
                         uint8_t *tag, size_t tag_len);

/**
 * \brief AES-256-GCM authenticated decryption using the on-chip OTP key.
 *
 * The authentication tag is verified. A mismatch returns a negative value.
 *
 * \param[in]  iv       Initialization vector / nonce (12 bytes for GCM).
 * \param[in]  iv_len   Length of \p iv in bytes.
 * \param[in]  aad      Additional authenticated data (may be NULL if aad_len==0).
 * \param[in]  aad_len  Length of \p aad in bytes.
 * \param[in]  ct       Ciphertext input.
 * \param[in]  len      Length of \p ct / \p pt in bytes.
 * \param[in]  tag      Expected authentication tag.
 * \param[in]  tag_len  Length of \p tag in bytes (typically 16).
 * \param[out] pt       Plaintext output (\p len bytes).
 *
 * \return 0 on success (tag verified), negative value on error / tag mismatch.
 */
int ameba_hw_gcm_decrypt(const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *ct, size_t len,
                         const uint8_t *tag, size_t tag_len,
                         uint8_t *pt);

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
