/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * AmebaDplus (RTL872XDA) implementation of the AES-GCM hardware adapter.
 *
 * Uses the ROM IPSEC crypto API with the on-chip OTP key (RDP key -> IPSEC
 * key slot 0). The raw key material never enters CPU RAM; rtl_crypto_aes_gcm_*
 * operate on the key loaded into the IPSEC engine by CRYPTO_OTPKey_Init.
 * The dummy Key[] array is only required to satisfy the ROM API signature.
 *
 * This mirrors the sdk0 amebadplus ITS implementation, wrapped behind the
 * shared ameba_hw_gcm interface. The AES clock is enabled by the BL2
 * BOOT_RccConfig() path (RCC_Config[] contains APBPeriph_AES), so no extra
 * clock setup is needed here.
 *
 * GCM IV length is fixed at 12 bytes by the ROM API.
 */

#include <ameba_soc.h>
#include <string.h>

#include "ameba_hw_gcm.h"

#define AMEBA_GCM_IV_LEN     12u
#define AMEBA_OTP_KEY_SLOT   0u

/* Scratch buffer for the empty-plaintext (len == 0) tag computation. It holds
 * the AAD bytes copied in as a "message" (see gcm_tag_over_aad below) and the
 * discarded ciphertext. Must be 32-byte (cache-line) aligned because the AES
 * DMA driver does DCache clean/invalidate over it. Sized generously above the
 * ITS AAD length (file_id 12 + data_size 4 + flags 4 = 20 bytes).
 */
#define AMEBA_GCM_SCRATCH_LEN  64u
static u8 __attribute__((aligned(32))) s_gcm_scratch_in[AMEBA_GCM_SCRATCH_LEN];
static u8 __attribute__((aligned(32))) s_gcm_scratch_ct[AMEBA_GCM_SCRATCH_LEN];

/*
 * Fixed 1-block dummy plaintext used only for the empty-payload tag helper.
 * Its content is arbitrary but must be identical on encrypt and decrypt so
 * the recomputed tag matches. It is domain-separated from any real ITS data
 * because it is only ever used when the actual plaintext length is zero.
 */
#define AMEBA_GCM_EMPTY_DUMMY_LEN  16u

/*
 * Empty-plaintext GCM tag helper (amebadplus IPSEC limitation workaround).
 *
 * The ROM IPSEC descriptor raises DES_ERR3 (err 0x2000) for a GCM operation
 * whose encryption length (enl) is zero -- pure GMAC / tag-only GCM over the
 * AAD alone is not supported by rtl_crypto_aes_gcm_* (the message->enl,
 * AAD->a2eo mapping means an empty message leaves enl==0). Every ITS asset
 * with real data works because it always has enl>0.
 *
 * To still authenticate a zero-length ITS asset (whose GCM tag must cover the
 * AAD = file_id + flags + data_size), we run a normal GCM operation with the
 * real AAD kept in the AAD field (a2eo>0, exactly as the working non-empty
 * path) plus a fixed dummy message (enl>0). The ciphertext is discarded --
 * the stored ITS ciphertext is zero-length -- and only the tag is kept.
 *
 * The tag is deterministic in (key, iv, aad, fixed-dummy), so encrypt and
 * decrypt reproduce the same value over identical metadata; tampering with
 * the AAD (file id / size / flags) changes the tag and is detected. This
 * helper never runs for len > 0, where the normal path already authenticates
 * plaintext + AAD.
 */
static int gcm_tag_over_aad(const uint8_t *iv, const uint8_t *aad,
                            size_t aad_len, uint8_t *tag)
{
    int ret;
    u8 dummy_key[32];

    if (aad == NULL || aad_len == 0) {
        return -1;
    }

    /* Fixed dummy plaintext (enl>0) so the IPSEC descriptor is well-formed. */
    (void)memset(s_gcm_scratch_in, 0, AMEBA_GCM_EMPTY_DUMMY_LEN);

    ret = rtl_crypto_aes_gcm_init(dummy_key, sizeof(dummy_key));
    if (ret == 0) {
        ret = rtl_crypto_aes_gcm_encrypt(s_gcm_scratch_in,
                                         AMEBA_GCM_EMPTY_DUMMY_LEN, iv,
                                         aad, (u32)aad_len,
                                         s_gcm_scratch_ct, tag);
    }

    return ret;
}

int ameba_hw_gcm_encrypt(const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *pt, size_t len,
                         uint8_t *ct,
                         uint8_t *tag, size_t tag_len)
{
    int ret;
    u8 dummy_key[32];

    if (iv == NULL || tag == NULL || (len != 0 && (pt == NULL || ct == NULL))) {
        return -1;
    }
    if (iv_len != AMEBA_GCM_IV_LEN || tag_len != 16u) {
        return -1;
    }

    /* Initialize AES engine and load the OTP/RDP key into the IPSEC slot. */
    CRYPTO_Init(NULL);
    CRYPTO_OTPKey_Init(AMEBA_OTP_KEY_SLOT, ENABLE);

    if (len == 0) {
        /* Empty plaintext: ROM GCM cannot GMAC the AAD alone. */
        ret = gcm_tag_over_aad(iv, aad, aad_len, tag);
    } else {
        /* dummy_key is unused (key comes from OTP) but required by the API. */
        ret = rtl_crypto_aes_gcm_init(dummy_key, sizeof(dummy_key));
        if (ret == 0) {
            ret = rtl_crypto_aes_gcm_encrypt(pt, (u32)len, iv,
                                             aad, (u32)aad_len, ct, tag);
        }
    }

    CRYPTO_OTPKey_Init(AMEBA_OTP_KEY_SLOT, DISABLE);

    return (ret == 0) ? 0 : -1;
}

int ameba_hw_gcm_decrypt(const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *ct, size_t len,
                         const uint8_t *tag, size_t tag_len,
                         uint8_t *pt)
{
    int ret;
    u8 dummy_key[32];
    /* rtl_crypto_aes_gcm_decrypt does not verify the tag internally; it
     * outputs the recomputed tag which we compare against the expected one.
     */
    u8 __attribute__((aligned(32))) calc_tag[32];

    if (iv == NULL || tag == NULL || (len != 0 && (pt == NULL || ct == NULL))) {
        return -1;
    }
    if (iv_len != AMEBA_GCM_IV_LEN || tag_len != 16u) {
        return -1;
    }

    CRYPTO_Init(NULL);
    CRYPTO_OTPKey_Init(AMEBA_OTP_KEY_SLOT, ENABLE);

    if (len == 0) {
        /* Mirror the encrypt path: recompute the tag over the AAD-as-message
         * (see gcm_tag_over_aad) and compare against the stored tag below.
         */
        ret = gcm_tag_over_aad(iv, aad, aad_len, calc_tag);
    } else {
        ret = rtl_crypto_aes_gcm_init(dummy_key, sizeof(dummy_key));
        if (ret == 0) {
            ret = rtl_crypto_aes_gcm_decrypt(ct, (u32)len, iv,
                                             aad, (u32)aad_len, pt, calc_tag);
        }
    }

    CRYPTO_OTPKey_Init(AMEBA_OTP_KEY_SLOT, DISABLE);

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
