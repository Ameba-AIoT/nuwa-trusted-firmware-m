/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Realtek shared implementation of the TF-M ITS AEAD HAL: ITS files are
 * protected with AES-GCM under a per-file key derived from the device-unique HUK
 * (ameba_its_key_get()). The nonce is a per-boot TRNG seed followed by a
 * monotonic counter, as in the upstream template.
 *
 * The derivation and the AEAD run on the ROM hardware engines rather than PSA
 * because ITS initializes before the Crypto service, so psa_aead_* /
 * psa_key_derivation_* would be a circular dependency.
 *
 * ITS partition buffers are only 4-byte aligned, so everything handed to the
 * DMA-driven engine is bounced through the cache-line aligned buffers below.
 * The payload is transformed in place, which halves the cost of the largest one
 * (see ameba_hw_gcm.h for why that is safe).
 */

#include <ameba_soc.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "config_tfm.h"
#include "tfm_hal_defs.h"
#include "platform/include/tfm_hal_its_encryption.h"

#include "ameba_hw_gcm.h"
#include "ameba_its_key.h"

#if TFM_ITS_ENC_NONCE_LENGTH != 12
#error "This implementation only supports an ITS nonce of size 12 (GCM)"
#endif

/* ITS data is bounded by ITS_MAX_ASSET_SIZE; AAD is 20 bytes (file_id + size +
 * flags). CACHE_LINE_* come from ameba_cache.h.
 */
#define ITS_AEAD_MAX_DATA        CACHE_LINE_ALIGNMENT(ITS_MAX_ASSET_SIZE)
#define ITS_AEAD_MAX_AAD         CACHE_LINE_SIZE
#define ITS_AEAD_IV_BUF          CACHE_LINE_SIZE
#define ITS_AEAD_TAG_BUF         CACHE_LINE_SIZE

/* The ITS partition is single threaded and calls these APIs sequentially, so
 * static buffers are safe and avoid large stack frames. s_data_buf is both the
 * source and the destination of the AEAD payload.
 */
static uint8_t ALIGNMTO(CACHE_LINE_SIZE) s_iv_buf[ITS_AEAD_IV_BUF];
static uint8_t ALIGNMTO(CACHE_LINE_SIZE) s_aad_buf[ITS_AEAD_MAX_AAD];
static uint8_t ALIGNMTO(CACHE_LINE_SIZE) s_data_buf[ITS_AEAD_MAX_DATA];
static uint8_t ALIGNMTO(CACHE_LINE_SIZE) s_tag_buf[ITS_AEAD_TAG_BUF];
/* Per-file AEAD key. Zeroized as soon as the operation completes. */
static uint8_t ALIGNMTO(CACHE_LINE_SIZE) s_key_buf[CACHE_LINE_SIZE];

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

    bad = (ctx->deriv_label == NULL) || (ctx->deriv_label_size == 0) ||
          (ctx->aad == NULL && ctx->aad_size != 0) ||
          (ctx->nonce == NULL && ctx->nonce_size != 0);

    return !bad;
}

/* Derive the per-file key into s_key_buf. */
static int its_aead_setup_key(struct tfm_hal_its_auth_crypt_ctx *ctx,
                              const uint8_t **key, size_t *key_len)
{
    if (ameba_its_key_get(ctx->deriv_label, ctx->deriv_label_size,
                          s_key_buf, key_len) != 0) {
        return -1;
    }

    *key = s_key_buf;

    return 0;
}

static void its_aead_clear_key(void)
{
    (void)memset(s_key_buf, 0, sizeof(s_key_buf));
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
    const uint8_t *key;
    size_t key_len;
    int rc;

    if (!ctx_is_valid(ctx) || tag == NULL) {
        return TFM_HAL_ERROR_INVALID_INPUT;
    }

    if (plaintext_size > ciphertext_size ||
        plaintext_size > sizeof(s_data_buf) ||
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
        (void)memcpy(s_data_buf, plaintext, plaintext_size);
    }

    if (its_aead_setup_key(ctx, &key, &key_len) != 0) {
        /* s_data_buf still holds the plaintext at this point. */
        (void)memset(s_data_buf, 0, plaintext_size);
        return TFM_HAL_ERROR_GENERIC;
    }

    rc = ameba_hw_gcm_encrypt(key, key_len,
                              s_iv_buf, ctx->nonce_size,
                              s_aad_buf, ctx->aad_size,
                              s_data_buf, plaintext_size,
                              s_data_buf,
                              s_tag_buf, tag_size);
    its_aead_clear_key();
    if (rc != 0) {
        (void)memset(s_data_buf, 0, plaintext_size);
        return TFM_HAL_ERROR_GENERIC;
    }

    if (plaintext_size != 0) {
        (void)memcpy(ciphertext, s_data_buf, plaintext_size);
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
    const uint8_t *key;
    size_t key_len;
    int rc;

    if (!ctx_is_valid(ctx) || tag == NULL) {
        return TFM_HAL_ERROR_INVALID_INPUT;
    }

    if (plaintext_size < ciphertext_size ||
        ciphertext_size > sizeof(s_data_buf) ||
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
        (void)memcpy(s_data_buf, ciphertext, ciphertext_size);
    }
    (void)memcpy(s_tag_buf, tag, tag_size);

    if (its_aead_setup_key(ctx, &key, &key_len) != 0) {
        return TFM_HAL_ERROR_GENERIC;
    }

    rc = ameba_hw_gcm_decrypt(key, key_len,
                              s_iv_buf, ctx->nonce_size,
                              s_aad_buf, ctx->aad_size,
                              s_data_buf, ciphertext_size,
                              s_tag_buf, tag_size,
                              s_data_buf);
    its_aead_clear_key();
    if (rc != 0) {
        /* Includes a tag mismatch, where the engine has already written the
         * unauthenticated plaintext into s_data_buf. Drop it either way.
         */
        (void)memset(s_data_buf, 0, ciphertext_size);
        return TFM_HAL_ERROR_GENERIC;
    }

    if (ciphertext_size != 0) {
        (void)memcpy(plaintext, s_data_buf, ciphertext_size);
    }

    return TFM_HAL_SUCCESS;
}
