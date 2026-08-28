/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Realtek shared implementation of the TF-M ITS AEAD HAL
 * (tfm_hal_its_aead_encrypt / decrypt / generate_nonce).
 *
 * Design (see report/ITS-crypto/):
 *   - ITS files are protected with AES-256-GCM.
 *   - The AEAD key is the on-chip IPSEC/OTP hardware key (S_IPSEC_KEY1); the
 *     raw key never enters CPU-addressable RAM. Because of this, and because
 *     the ITS partition initializes BEFORE the Crypto (PSA) service, we do NOT
 *     use psa_aead_* here (that would be a circular dependency, exactly the
 *     reason Nordic's platform rewrites this HAL to call its hardware driver
 *     directly). We instead call the Realtek ROM crypto engine through the
 *     ameba_hw_gcm adapter, which hides the amebag2 / amebadplus API
 *     differences.
 *   - The nonce is a random per-boot seed followed by a monotonic counter
 *     (same scheme as the upstream template), which avoids GCM nonce reuse.
 *     The random seed comes from the Realtek hardware TRNG via ameba_hw_random.
 *
 * The AES engine is DMA driven; the driver performs D-cache clean/invalidate
 * over the supplied ranges. To avoid corrupting adjacent data that shares a
 * boundary cache line (the ITS partition buffers are only 4-byte aligned), all
 * inputs/outputs are bounced through 32-byte (cache-line) aligned buffers.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "config_tfm.h"
#include "tfm_hal_defs.h"
#include "platform/include/tfm_hal_its_encryption.h"

#include "ameba_hw_gcm.h"

#if TFM_ITS_ENC_NONCE_LENGTH != 12
#error "This implementation only supports an ITS nonce of size 12 (GCM)"
#endif

/* Cache line size of the AmebaG2 / AmebaDplus AES DMA engine. */
#define ITS_AEAD_CACHE_LINE      32u
#define ITS_AEAD_ALIGN_UP(x)     (((x) + (ITS_AEAD_CACHE_LINE - 1u)) & \
                                  ~(ITS_AEAD_CACHE_LINE - 1u))

/* ITS data is bounded by ITS_MAX_ASSET_SIZE. */
#define ITS_AEAD_MAX_DATA        ITS_AEAD_ALIGN_UP(ITS_MAX_ASSET_SIZE)
/* AAD = file_id(12) + data_size(4) + flags(4) = 20 bytes; round to cache line. */
#define ITS_AEAD_MAX_AAD         ITS_AEAD_CACHE_LINE
#define ITS_AEAD_IV_BUF          ITS_AEAD_CACHE_LINE
#define ITS_AEAD_TAG_BUF         ITS_AEAD_CACHE_LINE

/* Cache-line aligned bounce buffers. The ITS partition is single threaded and
 * calls these APIs sequentially, so static buffers are safe and avoid large
 * stack frames.
 */
static uint8_t __attribute__((aligned(ITS_AEAD_CACHE_LINE)))
    s_iv_buf[ITS_AEAD_IV_BUF];
static uint8_t __attribute__((aligned(ITS_AEAD_CACHE_LINE)))
    s_aad_buf[ITS_AEAD_MAX_AAD];
static uint8_t __attribute__((aligned(ITS_AEAD_CACHE_LINE)))
    s_data_in_buf[ITS_AEAD_MAX_DATA];
static uint8_t __attribute__((aligned(ITS_AEAD_CACHE_LINE)))
    s_data_out_buf[ITS_AEAD_MAX_DATA];
static uint8_t __attribute__((aligned(ITS_AEAD_CACHE_LINE)))
    s_tag_buf[ITS_AEAD_TAG_BUF];

/* Nonce = per-boot random seed || monotonic counter. */
static uint32_t g_enc_counter;
static uint8_t  g_enc_nonce_seed[TFM_ITS_ENC_NONCE_LENGTH -
                                 sizeof(uint32_t)];

enum tfm_hal_status_t tfm_hal_its_aead_generate_nonce(uint8_t *nonce,
                                                      const size_t nonce_size)
{
    if (nonce == NULL) {
        return TFM_HAL_ERROR_INVALID_INPUT;
    }

    if (nonce_size != TFM_ITS_ENC_NONCE_LENGTH) {
        return TFM_HAL_ERROR_INVALID_INPUT;
    }

    if (g_enc_counter == UINT32_MAX) {
        return TFM_HAL_ERROR_GENERIC;
    }

    /* Draw a fresh random seed once per boot. */
    if (g_enc_counter == 0) {
        if (ameba_hw_random(g_enc_nonce_seed, sizeof(g_enc_nonce_seed)) != 0) {
            return TFM_HAL_ERROR_GENERIC;
        }
    }

    (void)memcpy(nonce, g_enc_nonce_seed, sizeof(g_enc_nonce_seed));
    (void)memcpy(nonce + sizeof(g_enc_nonce_seed),
                 &g_enc_counter, sizeof(g_enc_counter));

    g_enc_counter++;

    return TFM_HAL_SUCCESS;
}

static bool ctx_is_valid(struct tfm_hal_its_auth_crypt_ctx *ctx)
{
    bool bad;

    if (ctx == NULL) {
        return false;
    }

    bad = (ctx->deriv_label == NULL && ctx->deriv_label_size != 0) ||
          (ctx->aad == NULL && ctx->aad_size != 0) ||
          (ctx->nonce == NULL && ctx->nonce_size != 0);

    return !bad;
}

enum tfm_hal_status_t tfm_hal_its_aead_encrypt(
                                        struct tfm_hal_its_auth_crypt_ctx *ctx,
                                        const uint8_t *plaintext,
                                        const size_t plaintext_size,
                                        uint8_t *ciphertext,
                                        const size_t ciphertext_size,
                                        uint8_t *tag,
                                        const size_t tag_size)
{
    int rc;

    if (!ctx_is_valid(ctx) || tag == NULL) {
        return TFM_HAL_ERROR_INVALID_INPUT;
    }

    if (plaintext_size > ciphertext_size ||
        plaintext_size > sizeof(s_data_in_buf) ||
        ctx->nonce_size > sizeof(s_iv_buf) ||
        ctx->aad_size > sizeof(s_aad_buf) ||
        tag_size > sizeof(s_tag_buf)) {
        return TFM_HAL_ERROR_INVALID_INPUT;
    }

    /* Bounce inputs into cache-line aligned buffers. */
    (void)memcpy(s_iv_buf, ctx->nonce, ctx->nonce_size);
    if (ctx->aad_size != 0) {
        (void)memcpy(s_aad_buf, ctx->aad, ctx->aad_size);
    }
    if (plaintext_size != 0) {
        (void)memcpy(s_data_in_buf, plaintext, plaintext_size);
    }

    rc = ameba_hw_gcm_encrypt(s_iv_buf, ctx->nonce_size,
                              s_aad_buf, ctx->aad_size,
                              s_data_in_buf, plaintext_size,
                              s_data_out_buf,
                              s_tag_buf, tag_size);
    if (rc != 0) {
        return TFM_HAL_ERROR_GENERIC;
    }

    if (plaintext_size != 0) {
        (void)memcpy(ciphertext, s_data_out_buf, plaintext_size);
    }
    (void)memcpy(tag, s_tag_buf, tag_size);

    return TFM_HAL_SUCCESS;
}

enum tfm_hal_status_t tfm_hal_its_aead_decrypt(
                                        struct tfm_hal_its_auth_crypt_ctx *ctx,
                                        const uint8_t *ciphertext,
                                        const size_t ciphertext_size,
                                        uint8_t *tag,
                                        const size_t tag_size,
                                        uint8_t *plaintext,
                                        const size_t plaintext_size)
{
    int rc;

    if (!ctx_is_valid(ctx) || tag == NULL) {
        return TFM_HAL_ERROR_INVALID_INPUT;
    }

    if (plaintext_size < ciphertext_size ||
        ciphertext_size > sizeof(s_data_in_buf) ||
        ctx->nonce_size > sizeof(s_iv_buf) ||
        ctx->aad_size > sizeof(s_aad_buf) ||
        tag_size > sizeof(s_tag_buf)) {
        return TFM_HAL_ERROR_INVALID_INPUT;
    }

    (void)memcpy(s_iv_buf, ctx->nonce, ctx->nonce_size);
    if (ctx->aad_size != 0) {
        (void)memcpy(s_aad_buf, ctx->aad, ctx->aad_size);
    }
    if (ciphertext_size != 0) {
        (void)memcpy(s_data_in_buf, ciphertext, ciphertext_size);
    }
    (void)memcpy(s_tag_buf, tag, tag_size);

    rc = ameba_hw_gcm_decrypt(s_iv_buf, ctx->nonce_size,
                              s_aad_buf, ctx->aad_size,
                              s_data_in_buf, ciphertext_size,
                              s_tag_buf, tag_size,
                              s_data_out_buf);
    if (rc != 0) {
        /* Includes authentication-tag mismatch. */
        return TFM_HAL_ERROR_GENERIC;
    }

    if (ciphertext_size != 0) {
        (void)memcpy(plaintext, s_data_out_buf, ciphertext_size);
    }

    return TFM_HAL_SUCCESS;
}
