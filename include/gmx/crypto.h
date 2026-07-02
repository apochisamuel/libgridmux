/*
 * libgridmux - Session authentication and anti-replay
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_CRYPTO_H
#define GMX_CRYPTO_H

#include "types.h"
#include "error.h"

#define GMX_HMAC_KEY_SIZE     32
#define GMX_HMAC_DIGEST_SIZE  16
#define GMX_NONCE_TABLE_INIT  16

typedef struct gmx_nonce_entry {
    uint64_t nonce;
    uint32_t epoch;
    bool     used;
} gmx_nonce_entry_t;

typedef struct gmx_nonce_table {
    gmx_nonce_entry_t *entries;
    size_t             capacity;
    size_t             count;
} gmx_nonce_table_t;

typedef struct gmx_crypto_ctx {
    uint8_t            key[GMX_HMAC_KEY_SIZE];
    gmx_nonce_table_t  nonce_table;
    bool               initialized;
} gmx_crypto_ctx_t;

gmx_status_t  gmx_crypto_init(gmx_crypto_ctx_t *ctx, const uint8_t *key, size_t key_len);
void          gmx_crypto_destroy(gmx_crypto_ctx_t *ctx);

gmx_status_t  gmx_crypto_hmac(const gmx_crypto_ctx_t *ctx,
                                const uint8_t *data, size_t len,
                                uint8_t *digest);

gmx_status_t  gmx_crypto_verify_hmac(const gmx_crypto_ctx_t *ctx,
                                       const uint8_t *data, size_t len,
                                       const uint8_t *expected_digest);

gmx_status_t  gmx_crypto_nonce_table_init(gmx_nonce_table_t *nt, size_t capacity);
void          gmx_crypto_nonce_table_destroy(gmx_nonce_table_t *nt);

/* Verify nonce for a channel - indexes by channel_id */
gmx_status_t  gmx_crypto_verify_nonce(gmx_nonce_table_t *nt,
                                        uint32_t channel_id,
                                        uint64_t nonce,
                                        uint32_t epoch);

gmx_status_t  gmx_crypto_nonce_advance(gmx_nonce_table_t *nt,
                                         uint32_t channel_id);

/* Resize nonce table (used after migration imports new channels) */
gmx_status_t  gmx_crypto_nonce_table_resize(gmx_nonce_table_t *nt,
                                              size_t new_capacity);

#endif /* GMX_CRYPTO_H */
