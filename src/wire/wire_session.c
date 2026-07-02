/*
 * libgridmux - Wire-level session management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Session-level wire protocol operations: session header
 * validation, sequence tracking, and batch frame validation.
 */
#include "gmx/wire.h"
#include "gmx/crc.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

typedef struct gmx_wire_session_ctx {
    uint32_t session_id;
    uint32_t expected_epoch;
    uint32_t expected_sequence;
    uint32_t total_frames_processed;
    uint32_t total_bytes_processed;
    uint32_t crc_failures;
    uint32_t sequence_failures;
    bool     initialized;
} gmx_wire_session_ctx_t;

gmx_status_t gmx_wire_session_ctx_init(gmx_wire_session_ctx_t *ctx,
                                          uint32_t session_id)
{
    if (!ctx) return GMX_ERR_BAD_STATE;
    memset(ctx, 0, sizeof(*ctx));
    ctx->session_id = session_id;
    ctx->initialized = true;
    return GMX_OK;
}

gmx_status_t gmx_wire_session_validate_header(
    gmx_wire_session_ctx_t *ctx,
    const gmx_session_header_t *hdr)
{
    if (!ctx || !hdr) return GMX_ERR_BAD_STATE;

    if (hdr->session_id != ctx->session_id) return GMX_ERR_BAD_SESSION_ID;
    if (ctx->expected_epoch > 0 && hdr->epoch < ctx->expected_epoch)
        return GMX_ERR_BAD_EPOCH;

    return GMX_OK;
}

gmx_status_t gmx_wire_session_track_frame(gmx_wire_session_ctx_t *ctx,
                                             const gmx_frame_t *frame)
{
    if (!ctx || !frame) return GMX_ERR_BAD_STATE;

    if (frame->header.sequence != ctx->expected_sequence) {
        ctx->sequence_failures++;
        return GMX_ERR_BAD_SEQUENCE;
    }

    ctx->expected_sequence++;
    ctx->total_frames_processed++;
    ctx->total_bytes_processed += frame->header.length;

    return GMX_OK;
}

gmx_status_t gmx_wire_session_advance_epoch(gmx_wire_session_ctx_t *ctx,
                                               uint32_t new_epoch)
{
    if (!ctx) return GMX_ERR_BAD_STATE;
    if (new_epoch <= ctx->expected_epoch && ctx->expected_epoch > 0)
        return GMX_ERR_BAD_EPOCH;
    ctx->expected_epoch = new_epoch;
    return GMX_OK;
}

uint32_t gmx_wire_session_frames_processed(const gmx_wire_session_ctx_t *ctx)
{
    return ctx ? ctx->total_frames_processed : 0;
}

uint32_t gmx_wire_session_bytes_processed(const gmx_wire_session_ctx_t *ctx)
{
    return ctx ? ctx->total_bytes_processed : 0;
}

float gmx_wire_session_crc_failure_rate(const gmx_wire_session_ctx_t *ctx)
{
    if (!ctx || ctx->total_frames_processed == 0) return 0.0f;
    return (float)ctx->crc_failures / (float)ctx->total_frames_processed;
}

/*
 * Validate a batch of frames for consistency.
 * Checks sequence ordering, CRC validity, and session ID matching.
 * Returns the number of valid frames.
 */
uint32_t gmx_wire_validate_frame_batch(const uint8_t *data, size_t len,
                                         uint32_t session_id,
                                         uint32_t expected_start_seq,
                                         uint32_t max_frames)
{
    uint32_t valid_count = 0;
    size_t offset = 0;
    uint32_t seq = expected_start_seq;

    while (offset + GMX_FRAME_HEADER_WIRE_SIZE <= len &&
           valid_count < max_frames) {
        gmx_frame_t frame;
        size_t consumed = 0;
        gmx_status_t rc = gmx_wire_decode_frame(data + offset,
                                                  len - offset,
                                                  &frame, &consumed);
        if (rc != GMX_OK) break;

        if (frame.header.session_id != session_id) break;
        if (frame.header.sequence != seq) break;

        valid_count++;
        seq++;
        offset += consumed;
    }
    return valid_count;
}

/*
 * Compute a batch integrity hash over all frame headers.
 * Used for migration transfer verification.
 */
uint32_t gmx_wire_batch_integrity(const uint8_t *data, size_t len,
                                    uint16_t frame_count)
{
    uint32_t integrity = 0;
    size_t offset = GMX_SESSION_HEADER_WIRE_SIZE;

    for (uint16_t i = 0; i < frame_count && offset + GMX_FRAME_HEADER_WIRE_SIZE <= len; i++) {
        uint32_t frame_len = gmx_wire_read_u32(data + offset);
        if (frame_len > len - offset || frame_len < GMX_FRAME_HEADER_WIRE_SIZE) break;

        integrity = gmx_crc32c(integrity, data + offset, GMX_FRAME_HEADER_WIRE_SIZE);
        offset += frame_len;
    }
    return integrity;
}
