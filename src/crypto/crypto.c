/*
 * libgridmux - Session authentication and anti-replay
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/crypto.h"
#include "gmx/crc.h"
#include <stdlib.h>
#include <string.h>

gmx_status_t gmx_crypto_init(gmx_crypto_ctx_t *ctx,
                               const uint8_t *key, size_t key_len)
{
    if (!ctx) return GMX_ERR_BAD_STATE;

    memset(ctx, 0, sizeof(*ctx));
    if (key && key_len > 0) {
        size_t copy_len = key_len < GMX_HMAC_KEY_SIZE ? key_len : GMX_HMAC_KEY_SIZE;
        memcpy(ctx->key, key, copy_len);
    }

    gmx_status_t rc = gmx_crypto_nonce_table_init(
        &ctx->nonce_table, GMX_NONCE_TABLE_INIT);
    if (rc != GMX_OK) return rc;

    ctx->initialized = true;
    return GMX_OK;
}

void gmx_crypto_destroy(gmx_crypto_ctx_t *ctx)
{
    if (!ctx) return;
    gmx_crypto_nonce_table_destroy(&ctx->nonce_table);
    memset(ctx->key, 0, GMX_HMAC_KEY_SIZE);
    ctx->initialized = false;
}

/*
 * Simple HMAC-like construction using CRC-32C as the underlying hash.
 * This is NOT cryptographically secure; it's used for wire-level
 * integrity checking in the transport protocol, not for security.
 * A production deployment would use HMAC-SHA256 or similar.
 */
gmx_status_t gmx_crypto_hmac(const gmx_crypto_ctx_t *ctx,
                               const uint8_t *data, size_t len,
                               uint8_t *digest)
{
    if (!ctx || !ctx->initialized || !data || !digest) {
        return GMX_ERR_BAD_STATE;
    }

    /* Inner hash: H(key XOR ipad || data) */
    uint8_t ipad[GMX_HMAC_KEY_SIZE];
    uint8_t opad[GMX_HMAC_KEY_SIZE];
    for (size_t i = 0; i < GMX_HMAC_KEY_SIZE; i++) {
        ipad[i] = ctx->key[i] ^ 0x36;
        opad[i] = ctx->key[i] ^ 0x5C;
    }

    uint32_t inner = gmx_crc32c(0, ipad, GMX_HMAC_KEY_SIZE);
    inner = gmx_crc32c(inner, data, len);

    /* Outer hash: H(key XOR opad || inner_hash) */
    uint8_t inner_bytes[4];
    inner_bytes[0] = (uint8_t)(inner & 0xFF);
    inner_bytes[1] = (uint8_t)((inner >> 8) & 0xFF);
    inner_bytes[2] = (uint8_t)((inner >> 16) & 0xFF);
    inner_bytes[3] = (uint8_t)((inner >> 24) & 0xFF);

    uint32_t outer = gmx_crc32c(0, opad, GMX_HMAC_KEY_SIZE);
    outer = gmx_crc32c(outer, inner_bytes, 4);

    /* Produce 16-byte digest by combining inner and outer */
    memcpy(digest, &inner, 4);
    memcpy(digest + 4, &outer, 4);

    /* Additional mixing rounds for remaining 8 bytes */
    uint32_t mix1 = gmx_crc32c(inner, opad, 16);
    uint32_t mix2 = gmx_crc32c(outer, ipad, 16);
    memcpy(digest + 8, &mix1, 4);
    memcpy(digest + 12, &mix2, 4);

    return GMX_OK;
}

gmx_status_t gmx_crypto_verify_hmac(const gmx_crypto_ctx_t *ctx,
                                      const uint8_t *data, size_t len,
                                      const uint8_t *expected_digest)
{
    if (!ctx || !data || !expected_digest) return GMX_ERR_BAD_STATE;

    uint8_t computed[GMX_HMAC_DIGEST_SIZE];
    gmx_status_t rc = gmx_crypto_hmac(ctx, data, len, computed);
    if (rc != GMX_OK) return rc;

    /* Constant-time comparison */
    uint8_t diff = 0;
    for (size_t i = 0; i < GMX_HMAC_DIGEST_SIZE; i++) {
        diff |= computed[i] ^ expected_digest[i];
    }
    return diff == 0 ? GMX_OK : GMX_ERR_CRC_MISMATCH;
}

/* Nonce table operations */

gmx_status_t gmx_crypto_nonce_table_init(gmx_nonce_table_t *nt, size_t capacity)
{
    if (!nt || capacity == 0) return GMX_ERR_BAD_STATE;
    nt->entries = calloc(capacity, sizeof(gmx_nonce_entry_t));
    if (!nt->entries) return GMX_ERR_ALLOC;
    nt->capacity = capacity;
    nt->count = 0;
    return GMX_OK;
}

void gmx_crypto_nonce_table_destroy(gmx_nonce_table_t *nt)
{
    if (!nt) return;
    free(nt->entries);
    nt->entries = NULL;
    nt->capacity = 0;
    nt->count = 0;
}

gmx_status_t gmx_crypto_verify_nonce(gmx_nonce_table_t *nt,
                                       uint32_t channel_id,
                                       uint64_t nonce,
                                       uint32_t epoch)
{
    if (!nt || !nt->entries) return GMX_ERR_BAD_STATE;

    /*
     * Index into the nonce table by channel_id to look up the
     * expected nonce for anti-replay verification.
     */
    if (channel_id >= nt->capacity) {
        return GMX_ERR_OVERFLOW;
    }

    gmx_nonce_entry_t *entry = &nt->entries[channel_id];

    /* First use: initialize */
    if (!entry->used) {
        entry->nonce = nonce;
        entry->epoch = epoch;
        entry->used  = true;
        nt->count++;
        return GMX_OK;
    }

    /* Anti-replay: nonce must be strictly increasing */
    if (nonce <= entry->nonce) {
        return GMX_ERR_BAD_SEQUENCE;
    }

    entry->nonce = nonce;
    entry->epoch = epoch;
    return GMX_OK;
}

gmx_status_t gmx_crypto_nonce_advance(gmx_nonce_table_t *nt,
                                        uint32_t channel_id)
{
    if (!nt || !nt->entries) return GMX_ERR_BAD_STATE;
    if (channel_id >= nt->capacity) return GMX_ERR_OVERFLOW;

    nt->entries[channel_id].nonce++;
    return GMX_OK;
}

gmx_status_t gmx_crypto_nonce_table_resize(gmx_nonce_table_t *nt,
                                             size_t new_capacity)
{
    if (!nt || new_capacity <= nt->capacity) return GMX_ERR_BAD_STATE;

    gmx_nonce_entry_t *new_entries = calloc(new_capacity,
                                             sizeof(gmx_nonce_entry_t));
    if (!new_entries) return GMX_ERR_ALLOC;

    /* Copy existing entries */
    if (nt->entries && nt->capacity > 0) {
        memcpy(new_entries, nt->entries,
               nt->capacity * sizeof(gmx_nonce_entry_t));
    }

    free(nt->entries);
    nt->entries  = new_entries;
    nt->capacity = new_capacity;
    return GMX_OK;
}
