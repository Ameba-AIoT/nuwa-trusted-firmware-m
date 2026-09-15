/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * AmebaG2 (RTL8721F) implementation of the AES-GCM hardware adapter.
 *
 * Uses the ROM Key-Management + crypto one-shot APIs. The KM hardware loads the
 * key by key id, so the AES engine never fetches key material from RAM: the key
 * is written into the secure slot KM_AES_KEY_S_SW1 instead.
 *
 * crypto_gcm_auth_decrypt verifies the tag internally, so no manual comparison
 * is needed here.
 */

#include <ameba_soc.h>
#include <string.h>

#include "ameba_hw_gcm.h"

#define AMEBA_GCM_IV_LEN         12u

/* Secure software key slots for the ITS AEAD and its key derivation. Never
 * exposed to NS, and distinct so the KDF and the AEAD cannot clobber each other. */
#define AMEBA_GCM_SW_KEY_ID      KM_AES_KEY_S_SW1
#define AMEBA_HMAC_SW_KEY_ID     KM_HMAC_KEY_S_SW

static int key_len_to_bits(size_t key_len, u32 *key_len_bits)
{
    switch (key_len) {
    case 16u:
        *key_len_bits = KEY_BIT_128;
        break;
    case 24u:
        *key_len_bits = KEY_BIT_192;
        break;
    case 32u:
        *key_len_bits = KEY_BIT_256;
        break;
    default:
        return -1;
    }

    return 0;
}

/* Load the key into the software key slot and resolve its id / length. */
static int gcm_select_key(const uint8_t *key, size_t key_len,
                          u8 *key_id, u32 *key_len_bits)
{
    if (key == NULL) {
        return -1;
    }

    if (key_len_to_bits(key_len, key_len_bits) != 0) {
        return -1;
    }

    *key_id = AMEBA_GCM_SW_KEY_ID;

    if (crypto_aes_set_sw_key(*key_id, *key_len_bits, key) != RTK_SUCCESS) {
        return -1;
    }

    return 0;
}

int ameba_hw_gcm_encrypt(const uint8_t *key, size_t key_len,
                         const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *pt, size_t len,
                         uint8_t *ct,
                         uint8_t *tag, size_t tag_len)
{
    int ret;
    u8 key_id;
    u32 key_len_bits;

    if (iv == NULL || tag == NULL || (len != 0 && (pt == NULL || ct == NULL))) {
        return -1;
    }
    if (iv_len != AMEBA_GCM_IV_LEN || tag_len != 16u) {
        return -1;
    }

    /* Enable/prepare the AES engine (idempotent). */
    CRYPTO_Init();

    if (gcm_select_key(key, key_len, &key_id, &key_len_bits) != 0) {
        return -1;
    }

    ret = crypto_gcm_encrypt_and_tag(key_id, key_len_bits,
                                     1 /* is_encryption */, (u32)len,
                                     (u8 *)iv, (u32)iv_len,
                                     (u8 *)aad, (u32)aad_len,
                                     pt, ct,
                                     (u32)tag_len, tag);

    return (ret == 0) ? 0 : -1;
}

int ameba_hw_gcm_decrypt(const uint8_t *key, size_t key_len,
                         const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *ct, size_t len,
                         const uint8_t *tag, size_t tag_len,
                         uint8_t *pt)
{
    int ret;
    u8 key_id;
    u32 key_len_bits;

    if (iv == NULL || tag == NULL || (len != 0 && (pt == NULL || ct == NULL))) {
        return -1;
    }
    if (iv_len != AMEBA_GCM_IV_LEN || tag_len != 16u) {
        return -1;
    }

    CRYPTO_Init();

    if (gcm_select_key(key, key_len, &key_id, &key_len_bits) != 0) {
        return -1;
    }

    /* crypto_gcm_auth_decrypt verifies the tag internally. */
    ret = crypto_gcm_auth_decrypt(key_id, key_len_bits,
                                  (u32)len,
                                  (u8 *)iv, (u32)iv_len,
                                  (u8 *)aad, (u32)aad_len,
                                  ct, pt,
                                  (u32)tag_len, (u8 *)tag);

    return (ret == 0) ? 0 : -1;
}

/* HMAC-SHA256 via the KM software key slot + SHA engine. */
int ameba_hw_hmac_sha256(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t digest[AMEBA_HW_SHA256_LEN])
{
    SHA_context ctx;
    u32 key_len_bits;

    if (key == NULL || msg == NULL || digest == NULL || msg_len == 0) {
        return -1;
    }
    if (key_len_to_bits(key_len, &key_len_bits) != 0) {
        return -1;
    }

    CRYPTO_Init();

    if (crypto_hmac_sha2_set_sw_key(AMEBA_HMAC_SW_KEY_ID, key_len_bits,
                                    key) != RTK_SUCCESS) {
        return -1;
    }

    if (crypto_hmac_sha2_init(&ctx, SHA_256, AMEBA_HMAC_SW_KEY_ID,
                              key_len_bits) != RTK_SUCCESS) {
        return -1;
    }

    /* Read-only mode: no ciphertext/copy destination. */
    if (crypto_hmac_sha2_update(&ctx, msg, NULL, msg_len) != RTK_SUCCESS) {
        return -1;
    }

    if (crypto_hmac_sha2_final(&ctx, digest) != RTK_SUCCESS) {
        return -1;
    }

    return 0;
}

int ameba_hw_random(uint8_t *buf, size_t len)
{
    if (buf == NULL) {
        return -1;
    }
    if (TRNG_get_random_bytes(buf, (u32)len) != 0) {
        return -1;
    }
    return 0;
}
