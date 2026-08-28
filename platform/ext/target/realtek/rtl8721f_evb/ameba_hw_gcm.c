/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * AmebaG2 (RTL8721F) implementation of the AES-GCM hardware adapter.
 *
 * Uses the ROM Key-Management + crypto GCM one-shot API with the on-chip
 * secure IPSEC OTP key (KM_AES_KEY_S_IPSEC_KEY1, OTP raw address 0x200,
 * 256-bit). The KM hardware loads the OTP key by key id; the raw key never
 * enters CPU-addressable memory, so no key material is passed here.
 *
 * crypto_gcm_auth_decrypt verifies the authentication tag internally and
 * returns non-zero on mismatch, so no manual tag comparison is needed.
 *
 * GCM IV length is 12 bytes.
 */

#include <ameba_soc.h>
#include <string.h>

#include "ameba_hw_gcm.h"

#define AMEBA_GCM_IV_LEN     12u
#define AMEBA_GCM_KEY_ID     KM_AES_KEY_S_IPSEC_KEY1
#define AMEBA_GCM_KEY_BITS   KEY_BIT_256

int ameba_hw_gcm_encrypt(const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *pt, size_t len,
                         uint8_t *ct,
                         uint8_t *tag, size_t tag_len)
{
    int ret;

    if (iv == NULL || tag == NULL || (len != 0 && (pt == NULL || ct == NULL))) {
        return -1;
    }
    if (iv_len != AMEBA_GCM_IV_LEN || tag_len != 16u) {
        return -1;
    }

    /* Enable/prepare the AES engine (idempotent). */
    CRYPTO_Init();

    ret = crypto_gcm_encrypt_and_tag(AMEBA_GCM_KEY_ID, AMEBA_GCM_KEY_BITS,
                                     1 /* is_encryption */, (u32)len,
                                     (u8 *)iv, (u32)iv_len,
                                     (u8 *)aad, (u32)aad_len,
                                     pt, ct,
                                     (u32)tag_len, tag);

    return (ret == 0) ? 0 : -1;
}

int ameba_hw_gcm_decrypt(const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *ct, size_t len,
                         const uint8_t *tag, size_t tag_len,
                         uint8_t *pt)
{
    int ret;

    if (iv == NULL || tag == NULL || (len != 0 && (pt == NULL || ct == NULL))) {
        return -1;
    }
    if (iv_len != AMEBA_GCM_IV_LEN || tag_len != 16u) {
        return -1;
    }

    CRYPTO_Init();

    /* crypto_gcm_auth_decrypt verifies the tag internally. */
    ret = crypto_gcm_auth_decrypt(AMEBA_GCM_KEY_ID, AMEBA_GCM_KEY_BITS,
                                  (u32)len,
                                  (u8 *)iv, (u32)iv_len,
                                  (u8 *)aad, (u32)aad_len,
                                  ct, pt,
                                  (u32)tag_len, (u8 *)tag);

    return (ret == 0) ? 0 : -1;
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
