/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * ITS AEAD key derivation from the HUK, see ameba_its_key.h. DerivedKey depends
 * on SecEpoch: bumping the anti-rollback epoch changes every ITS key and the ITS
 * partition must be erased.
 */

#include <ameba_soc.h>

#include <stdint.h>
#include <string.h>

#include "psa_manifest/pid.h"

#include "ameba_its_key.h"
#include "ameba_hw_gcm.h"

#define ITS_KDF_PRK_LEN     AMEBA_HW_SHA256_LEN

/* Holds either the 16-byte IKM or the expand message (17 bytes for a 12-byte
 * ITS file id). */
#define ITS_KDF_MSG_MAX     32u

/* DMA destination, needs an exclusive cache line. The HMAC key and message are
 * inputs only, so 4-byte alignment on the stack is enough for those. */
static uint8_t ALIGNMTO(CACHE_LINE_SIZE) s_digest[AMEBA_HW_SHA256_LEN];

/* HUK-derived secret, see startup_amebag2.c / startup_amebadplus.c. */
extern uint8_t DerivedKey_Bkup[16];

/*
 * out = HMAC(HMAC(key = empty, ikm), info1 || info2 || 0x01)[0..15]
 *
 * The info parts are concatenated in msg, so no extra buffer is needed. The empty
 * salt is passed as 32 zero bytes because the AmebaG2 KM software key slot only
 * takes 128/192/256-bit keys; HMAC zero-pads either to the same block.
 */
static int its_hkdf16(const uint8_t *ikm, size_t ikm_len,
                      const uint8_t *info1, size_t info1_len,
                      const uint8_t *info2, size_t info2_len,
                      uint8_t out[AMEBA_ITS_KEY_LEN])
{
    uint8_t ALIGNMTO(sizeof(uint32_t)) msg[ITS_KDF_MSG_MAX];
    uint8_t ALIGNMTO(sizeof(uint32_t)) prf_key[ITS_KDF_PRK_LEN];
    size_t info_len = info1_len + info2_len;
    int rc;

    if (ikm_len > sizeof(msg) || info_len + 1u > sizeof(msg)) {
        return -1;
    }

    /* Extract. */
    (void)memset(prf_key, 0, sizeof(prf_key));
    (void)memcpy(msg, ikm, ikm_len);
    rc = ameba_hw_hmac_sha256(prf_key, sizeof(prf_key),
                              msg, ikm_len, s_digest);
    (void)memset(msg, 0, ikm_len);
    if (rc != 0) {
        goto out_clear;
    }

    /* Expand, single block. */
    (void)memcpy(prf_key, s_digest, sizeof(prf_key));
    (void)memcpy(msg, info1, info1_len);
    (void)memcpy(msg + info1_len, info2, info2_len);
    msg[info_len] = 0x01;
    rc = ameba_hw_hmac_sha256(prf_key, sizeof(prf_key),
                              msg, info_len + 1u, s_digest);
    (void)memset(msg, 0, info_len + 1u);
    if (rc != 0) {
        goto out_clear;
    }

    (void)memcpy(out, s_digest, AMEBA_ITS_KEY_LEN);

out_clear:
    (void)memset(prf_key, 0, sizeof(prf_key));
    (void)memset(s_digest, 0, sizeof(s_digest));

    return (rc == 0) ? 0 : -1;
}

int ameba_its_key_get(const uint8_t *label, size_t label_len,
                      uint8_t key[AMEBA_ITS_KEY_LEN], size_t *key_len)
{
    const int32_t user = TFM_SP_ITS;
    int rc;

    if (label == NULL || label_len == 0 || key == NULL || key_len == NULL) {
        return -1;
    }

    /* The partition id is fixed length, so the concatenation is unambiguous. */
    rc = its_hkdf16(DerivedKey_Bkup, sizeof(DerivedKey_Bkup),
                    (const uint8_t *)&user, sizeof(user),
                    label, label_len, key);
    if (rc != 0) {
        return -1;
    }

    *key_len = AMEBA_ITS_KEY_LEN;

    return 0;
}
