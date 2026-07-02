/*
 * libgridmux - Journal replay for crash recovery
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/journal.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_REPLAY_WINDOW_SIZE  16

typedef struct gmx_journal_replay_ctx {
    uint32_t  start_segment;
    uint32_t  end_segment;
    uint32_t  current_segment;
    uint32_t  bytes_replayed;
    uint32_t  segments_replayed;
    bool      active;
    bool      complete;
} gmx_journal_replay_ctx_t;

gmx_status_t gmx_journal_replay_init(gmx_journal_replay_ctx_t *ctx,
                                        uint32_t start_seg,
                                        uint32_t end_seg)
{
    if (!ctx) return GMX_ERR_BAD_STATE;
    memset(ctx, 0, sizeof(*ctx));
    ctx->start_segment   = start_seg;
    ctx->end_segment     = end_seg;
    ctx->current_segment = start_seg;
    ctx->active = true;
    return GMX_OK;
}

gmx_status_t gmx_journal_replay_step(gmx_journal_replay_ctx_t *ctx,
                                        gmx_journal_t *journal,
                                        uint8_t *dst, size_t dst_cap,
                                        size_t *out_len)
{
    if (!ctx || !ctx->active || !journal || !dst || !out_len)
        return GMX_ERR_BAD_STATE;

    if (ctx->current_segment > ctx->end_segment) {
        ctx->complete = true;
        *out_len = 0;
        return GMX_OK;
    }

    gmx_journal_segment_t *seg = gmx_journal_lookup_segment(
        journal, ctx->current_segment);
    if (!seg) {
        ctx->current_segment++;
        *out_len = 0;
        return GMX_OK;
    }

    size_t copy_len = seg->data_len < dst_cap ? seg->data_len : dst_cap;
    memcpy(dst, seg->data, copy_len);
    *out_len = copy_len;

    ctx->bytes_replayed += (uint32_t)copy_len;
    ctx->segments_replayed++;
    ctx->current_segment++;

    return GMX_OK;
}

bool gmx_journal_replay_is_complete(const gmx_journal_replay_ctx_t *ctx)
{
    return ctx ? ctx->complete : true;
}

uint32_t gmx_journal_replay_progress(const gmx_journal_replay_ctx_t *ctx)
{
    if (!ctx || ctx->end_segment <= ctx->start_segment) return 100;
    uint32_t total = ctx->end_segment - ctx->start_segment + 1;
    uint32_t done = ctx->current_segment - ctx->start_segment;
    if (done >= total) return 100;
    return (done * 100) / total;
}
