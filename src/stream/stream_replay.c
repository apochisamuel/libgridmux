/*
 * libgridmux - Stream replay from journal checkpoints
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Implements stream replay by walking checkpoint entries from
 * the journal system, reconstructing the stream's data at a
 * specific epoch for crash recovery or migration.
 */
#include "gmx/stream.h"
#include "gmx/journal.h"
#include "gmx/lease.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_REPLAY_BUF_SIZE     8192
#define GMX_REPLAY_MAX_SEGMENTS 32

typedef struct gmx_stream_replay_ctx {
    uint32_t  stream_id;
    uint32_t  target_epoch;
    uint32_t  target_sequence;
    uint32_t  segments_replayed;
    uint32_t  bytes_replayed;
    uint8_t  *replay_buffer;
    size_t    replay_buf_len;
    size_t    replay_buf_cap;
    bool      active;
    bool      complete;
} gmx_stream_replay_ctx_t;

gmx_status_t gmx_stream_replay_init(gmx_stream_replay_ctx_t *ctx,
                                       uint32_t stream_id,
                                       uint32_t target_epoch)
{
    if (!ctx) return GMX_ERR_BAD_STATE;
    memset(ctx, 0, sizeof(*ctx));
    ctx->stream_id = stream_id;
    ctx->target_epoch = target_epoch;
    ctx->replay_buf_cap = GMX_REPLAY_BUF_SIZE;
    ctx->replay_buffer = malloc(ctx->replay_buf_cap);
    if (!ctx->replay_buffer) return GMX_ERR_ALLOC;
    ctx->active = true;
    return GMX_OK;
}

void gmx_stream_replay_destroy(gmx_stream_replay_ctx_t *ctx)
{
    if (!ctx) return;
    free(ctx->replay_buffer);
    ctx->replay_buffer = NULL;
    ctx->active = false;
}

gmx_status_t gmx_stream_replay_segment(gmx_stream_replay_ctx_t *ctx,
                                          const uint8_t *seg_data,
                                          size_t seg_len)
{
    if (!ctx || !ctx->active || !seg_data) return GMX_ERR_BAD_STATE;
    if (ctx->complete) return GMX_ERR_BAD_STATE;

    size_t space = ctx->replay_buf_cap - ctx->replay_buf_len;
    size_t to_copy = seg_len < space ? seg_len : space;

    if (to_copy > 0) {
        memcpy(ctx->replay_buffer + ctx->replay_buf_len, seg_data, to_copy);
        ctx->replay_buf_len += to_copy;
        ctx->bytes_replayed += (uint32_t)to_copy;
    }
    ctx->segments_replayed++;

    return GMX_OK;
}

gmx_status_t gmx_stream_replay_from_journal(
    gmx_stream_replay_ctx_t *ctx,
    gmx_journal_t *journal,
    uint32_t start_segment_id,
    uint32_t max_segments)
{
    if (!ctx || !ctx->active || !journal) return GMX_ERR_BAD_STATE;

    uint32_t replayed = 0;
    for (uint32_t sid = start_segment_id;
         sid < start_segment_id + max_segments && replayed < GMX_REPLAY_MAX_SEGMENTS;
         sid++) {
        gmx_journal_segment_t *seg = gmx_journal_lookup_segment(journal, sid);
        if (!seg) continue;
        if (seg->epoch_created > ctx->target_epoch) break;

        gmx_status_t rc = gmx_stream_replay_segment(ctx, seg->data, seg->data_len);
        if (rc != GMX_OK) return rc;
        replayed++;
    }

    if (replayed > 0) ctx->target_sequence += replayed;
    return GMX_OK;
}

gmx_status_t gmx_stream_replay_from_lease(
    gmx_stream_replay_ctx_t *ctx,
    gmx_lease_t *lease,
    gmx_journal_t *journal)
{
    if (!ctx || !ctx->active || !lease || !journal) return GMX_ERR_BAD_STATE;

    if (!lease->segment_ref) return GMX_ERR_NOT_FOUND;

    uint8_t tmp[GMX_REPLAY_BUF_SIZE];
    size_t mat_len = 0;

    gmx_status_t rc = gmx_journal_materialize(journal, lease->segment_ref,
                                                tmp, sizeof(tmp), &mat_len);
    if (rc != GMX_OK) return rc;

    return gmx_stream_replay_segment(ctx, tmp, mat_len);
}

gmx_status_t gmx_stream_replay_finalize(gmx_stream_replay_ctx_t *ctx,
                                           uint8_t *dst, size_t dst_cap,
                                           size_t *out_len)
{
    if (!ctx || !ctx->active || !dst || !out_len) return GMX_ERR_BAD_STATE;

    size_t to_copy = ctx->replay_buf_len < dst_cap ? ctx->replay_buf_len : dst_cap;
    memcpy(dst, ctx->replay_buffer, to_copy);
    *out_len = to_copy;
    ctx->complete = true;

    return GMX_OK;
}

bool gmx_stream_replay_is_complete(const gmx_stream_replay_ctx_t *ctx)
{
    return ctx ? ctx->complete : false;
}

uint32_t gmx_stream_replay_bytes_replayed(const gmx_stream_replay_ctx_t *ctx)
{
    return ctx ? ctx->bytes_replayed : 0;
}
