/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * AmebaDplus (RTL872XDA) implementation of the AES-GCM / HMAC-SHA256 adapter.
 *
 * Uses the ROM IPSEC crypto API, whose GCM IV length is fixed at 12 bytes. The
 * key is passed straight to rtl_crypto_aes_gcm_init().
 *
 * The AES/SHA clocks are already enabled by the BL2 BOOT_RccConfig() path, so no
 * extra clock setup is needed.
 */

#include <ameba_soc.h>
#include <string.h>

#include "ameba_hw_gcm.h"

#define AMEBA_GCM_IV_LEN     12u

/* Dummy message and discarded ciphertext of the empty-payload tag helper below;
 * one cache line covers both. DMA destinations, hence the alignment. */
#define AMEBA_GCM_SCRATCH_LEN  CACHE_LINE_SIZE
static u8 ALIGNMTO(CACHE_LINE_SIZE) s_gcm_scratch_in[AMEBA_GCM_SCRATCH_LEN];
static u8 ALIGNMTO(CACHE_LINE_SIZE) s_gcm_scratch_ct[AMEBA_GCM_SCRATCH_LEN];

/* Cache-line aligned copy of the key handed to the ROM API (DMA driven). */
static u8 ALIGNMTO(CACHE_LINE_SIZE) s_key_buf[32];

#define AMEBA_GCM_EMPTY_DUMMY_LEN  16u

/* Load the key into the engine, 0 on success. */
static int gcm_key_init(const uint8_t *key, size_t key_len)
{
    if (key == NULL) {
        return -1;
    }

    if (key_len != 16u && key_len != 24u && key_len != 32u) {
        return -1;
    }

    (void)memcpy(s_key_buf, key, key_len);

    return (rtl_crypto_aes_gcm_init(s_key_buf, (u32)key_len) == 0) ? 0 : -1;
}

static void gcm_key_deinit(void)
{
    (void)memset(s_key_buf, 0, sizeof(s_key_buf));
}

/*
 * Empty-plaintext GCM tag helper: the ROM IPSEC descriptor raises DES_ERR3
 * (0x2000) when the encryption length is zero, so rtl_crypto_aes_gcm_* cannot
 * GMAC the AAD alone. Encrypt a fixed dummy message instead, keeping the real
 * AAD in the AAD field, and discard the ciphertext. The tag stays deterministic
 * in (key, iv, aad), so decrypt reproduces it and AAD tampering is still
 * detected. Uses whatever key gcm_key_init() loaded.
 */
static int gcm_tag_over_aad(const uint8_t *iv, const uint8_t *aad,
                            size_t aad_len, uint8_t *tag)
{
    if (aad == NULL || aad_len == 0) {
        return -1;
    }

    (void)memset(s_gcm_scratch_in, 0, AMEBA_GCM_EMPTY_DUMMY_LEN);

    return rtl_crypto_aes_gcm_encrypt(s_gcm_scratch_in,
                                      AMEBA_GCM_EMPTY_DUMMY_LEN, iv,
                                      aad, (u32)aad_len,
                                      s_gcm_scratch_ct, tag);
}

int ameba_hw_gcm_encrypt(const uint8_t *key, size_t key_len,
                         const uint8_t *iv, size_t iv_len,
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

    /* Initialize AES engine. */
    CRYPTO_Init(NULL);

    if (gcm_key_init(key, key_len) != 0) {
        gcm_key_deinit();
        return -1;
    }

    if (len == 0) {
        /* Empty plaintext: ROM GCM cannot GMAC the AAD alone. */
        ret = gcm_tag_over_aad(iv, aad, aad_len, tag);
    } else {
        ret = rtl_crypto_aes_gcm_encrypt(pt, (u32)len, iv,
                                         aad, (u32)aad_len, ct, tag);
    }

    gcm_key_deinit();

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
    /* rtl_crypto_aes_gcm_decrypt outputs the recomputed tag rather than
     * verifying it, so the comparison is done here. */
    u8 ALIGNMTO(CACHE_LINE_SIZE) calc_tag[32];

    if (iv == NULL || tag == NULL || (len != 0 && (pt == NULL || ct == NULL))) {
        return -1;
    }
    if (iv_len != AMEBA_GCM_IV_LEN || tag_len != 16u) {
        return -1;
    }

    CRYPTO_Init(NULL);

    if (gcm_key_init(key, key_len) != 0) {
        gcm_key_deinit();
        return -1;
    }

    if (len == 0) {
        ret = gcm_tag_over_aad(iv, aad, aad_len, calc_tag);
    } else {
        ret = rtl_crypto_aes_gcm_decrypt(ct, (u32)len, iv,
                                         aad, (u32)aad_len, pt, calc_tag);
    }

    gcm_key_deinit();

    if (ret != 0) {
        return -1;
    }

    /* Constant-time-ish tag comparison. */
    {
        uint8_t diff = 0;
        size_t i;
        for (i = 0; i < tag_len; i++) {
            diff |= (uint8_t)(calc_tag[i] ^ tag[i]);
        }
        if (diff != 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * HMAC-SHA256 via the ROM SHA engine, which has its own adapter: CRYPTO_SHA_Init()
 * is required on top of CRYPTO_Init() or rtl_crypto_hmac_sha2_init() asserts.
 * Buffers are sized for the only caller, the ITS key derivation (32-byte PRK
 * key, 16-byte IKM message).
 */
#define AMEBA_HMAC_KEY_MAX   32u
#define AMEBA_HMAC_MSG_MAX   32u

static u8 ALIGNMTO(CACHE_LINE_SIZE) s_hmac_key[AMEBA_HMAC_KEY_MAX];
static u8 ALIGNMTO(CACHE_LINE_SIZE) s_hmac_msg[AMEBA_HMAC_MSG_MAX];
static u8 ALIGNMTO(CACHE_LINE_SIZE) s_hmac_digest[32];

int ameba_hw_hmac_sha256(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t digest[AMEBA_HW_SHA256_LEN])
{
    int ret;

    if (key == NULL || msg == NULL || digest == NULL) {
        return -1;
    }
    if (key_len == 0 || key_len > AMEBA_HMAC_KEY_MAX ||
        msg_len == 0 || msg_len > AMEBA_HMAC_MSG_MAX) {
        return -1;
    }

    CRYPTO_SHA_Init(NULL);

    (void)memset(s_hmac_key, 0, sizeof(s_hmac_key));
    (void)memset(s_hmac_msg, 0, sizeof(s_hmac_msg));
    (void)memcpy(s_hmac_key, key, key_len);
    (void)memcpy(s_hmac_msg, msg, msg_len);

    ret = rtl_crypto_hmac_sha2(SHA2_256, s_hmac_msg, (u32)msg_len,
                               s_hmac_key, (u32)key_len, s_hmac_digest);

    (void)memset(s_hmac_key, 0, sizeof(s_hmac_key));
    (void)memset(s_hmac_msg, 0, sizeof(s_hmac_msg));

    if (ret != 0) {
        (void)memset(s_hmac_digest, 0, sizeof(s_hmac_digest));
        return -1;
    }

    (void)memcpy(digest, s_hmac_digest, AMEBA_HW_SHA256_LEN);
    (void)memset(s_hmac_digest, 0, sizeof(s_hmac_digest));

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
