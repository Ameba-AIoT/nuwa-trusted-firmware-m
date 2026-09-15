/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* ITS AEAD key provider: per-file key derived from the HUK secret. */

#ifndef AMEBA_ITS_KEY_H
#define AMEBA_ITS_KEY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Matches TFM_ITS_KEY_LENGTH. */
#define AMEBA_ITS_KEY_LEN   16u

/**
 * \brief Obtain the AEAD key for one ITS file (\p label is the file id).
 *
 * One HKDF-SHA256 on the on-chip HMAC engine, empty salt, truncated to 16 bytes:
 *
 *   file_key = HMAC(HMAC(0, DerivedKey_Bkup),
 *                   int32 TFM_SP_ITS || label || 0x01)[0..15]
 *
 * The partition id separates ITS from the other HUK users, the label separates
 * the files. Changing this construction invalidates stored ITS data.
 *
 * On an unprovisioned device the HUK secret is all zero and the derived keys are
 * the same on every such device. That is not rejected here, see the comment in
 * ameba_its_key.c.
 *
 * \return 0 on success, negative on error.
 */
int ameba_its_key_get(const uint8_t *label, size_t label_len,
                      uint8_t key[AMEBA_ITS_KEY_LEN], size_t *key_len);

#ifdef __cplusplus
}
#endif

#endif /* AMEBA_ITS_KEY_H */
