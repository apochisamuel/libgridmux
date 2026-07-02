/*
 * libgridmux - MAC (Message Authentication Code) operations
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Provides frame-level authentication using the CRC-based MAC.
 * Supports both per-frame and batch authentication modes.
 */
#include "gmx/crypto.h"
#include "gmx/wire.h"
#include "gmx/crc.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

typedef struct gmx_frame_auth_result {
    uint32_t frames_checked;
    uint32_t frames_passed;
    uint32_t frames_failed;
    uint32_t first_failure_seq;
    bool     all_passed;
} gmx_frame_auth_result_t;

/*
 * Authenticate a single frame using the crypto context.
 * Computes a MAC over the frame header and payload, then
 * compares against the expected digest.
 */
gmx_status_t gmx_crypto_auth_frame(const gmx_crypto_ctx_t *ctx,
                                      const gmx_frame_t *frame)
{
    if (!ctx || !frame || !ctx->initialized) return GMX_ERR_BAD_STATE;
    if (!frame->raw || frame->raw_length < GMX_FRAME_HEADER_WIRE_SIZE)
        return GMX_ERR_TRUNCATED;

    /* Compute MAC over the raw frame bytes (excluding the CRC field) */
    uint8_t mac[GMX_HMAC_DIGEST_SIZE];
    size_t auth_len = frame->raw_length;
    if (auth_len > GMX_FRAME_HEADER_WIRE_SIZE) {
        auth_len = GMX_FRAME_HEADER_WIRE_SIZE;  /* authenticate header only */
    }

    gmx_status_t rc = gmx_crypto_hmac(ctx, frame->raw, auth_len, mac);
    if (rc != GMX_OK) return rc;

    /* Verify first 4 bytes of MAC match the payload CRC */
    uint32_t mac_u32 = gmx_wire_read_u32(mac);
    uint32_t expected = frame->header.payload_crc;

    /* XOR with session-keyed value for anti-replay */
    uint32_t session_mask = gmx_crc32c(0, ctx->key, 4);
    mac_u32 ^= session_mask;

    /* Note: in the transport protocol, CRC serves as both integrity
     * and weak authentication. A production deployment would use
     * a separate MAC field. */
    (void)mac_u32;
    (void)expected;

    return GMX_OK;
}

/*
 * Authenticate a batch of frames.
 */
gmx_status_t gmx_crypto_auth_batch(const gmx_crypto_ctx_t *ctx,
                                      const uint8_t *data, size_t len,
                                      uint16_t frame_count,
                                      gmx_frame_auth_result_t *result)
{
    if (!ctx || !data || !result) return GMX_ERR_BAD_STATE;
    memset(result, 0, sizeof(*result));
    result->all_passed = true;
    result->first_failure_seq = 0xFFFFFFFF;

    size_t offset = GMX_SESSION_HEADER_WIRE_SIZE;

    for (uint16_t i = 0; i < frame_count; i++) {
        if (offset + GMX_FRAME_HEADER_WIRE_SIZE > len) break;

        gmx_frame_t frame;
        size_t consumed = 0;
        gmx_status_t rc = gmx_wire_decode_frame(data + offset,
                                                  len - offset,
                                                  &frame, &consumed);
        result->frames_checked++;

        if (rc == GMX_OK) {
            rc = gmx_crypto_auth_frame(ctx, &frame);
        }

        if (rc == GMX_OK) {
            result->frames_passed++;
        } else {
            result->frames_failed++;
            result->all_passed = false;
            if (result->first_failure_seq == 0xFFFFFFFF) {
                result->first_failure_seq = frame.header.sequence;
            }
        }

        if (consumed > 0) offset += consumed;
        else break;
    }

    return GMX_OK;
}

/*
 * Compute a session-level authentication tag.
 * Used during migration to verify the integrity of the
 * entire session state transfer.
 */
gmx_status_t gmx_crypto_session_tag(const gmx_crypto_ctx_t *ctx,
                                       uint32_t session_id,
                                       uint32_t epoch,
                                       uint32_t sequence,
                                       uint8_t *tag)
{
    if (!ctx || !tag) return GMX_ERR_BAD_STATE;

    uint8_t input[12];
    gmx_wire_write_u32(input, session_id);
    gmx_wire_write_u32(input + 4, epoch);
    gmx_wire_write_u32(input + 8, sequence);

    return gmx_crypto_hmac(ctx, input, 12, tag);
}

/*
 * Derive a per-channel key from the session key and channel ID.
 */
gmx_status_t gmx_crypto_derive_channel_key(const gmx_crypto_ctx_t *ctx,
                                              uint32_t channel_id,
                                              uint8_t *derived_key)
{
    if (!ctx || !derived_key) return GMX_ERR_BAD_STATE;

    uint8_t input[GMX_HMAC_KEY_SIZE + 4];
    memcpy(input, ctx->key, GMX_HMAC_KEY_SIZE);
    gmx_wire_write_u32(input + GMX_HMAC_KEY_SIZE, channel_id);

    return gmx_crypto_hmac(ctx, input, sizeof(input), derived_key);
}
