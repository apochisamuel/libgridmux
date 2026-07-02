/*
 * libgridmux - Migration transfer state and channel re-anchoring
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/migrate.h"
#include "gmx/channel.h"
#include "gmx/stream.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_TRANSFER_MAX_STREAMS   32

typedef struct gmx_transfer_state {
    uint32_t migration_id;
    uint32_t stream_ids[GMX_TRANSFER_MAX_STREAMS];
    uint32_t stream_count;
    uint32_t bytes_transferred;
    uint32_t frames_transferred;
    bool     drain_complete;
} gmx_transfer_state_t;

gmx_status_t gmx_transfer_state_init(gmx_transfer_state_t *ts,
                                        uint32_t migration_id)
{
    if (!ts) return GMX_ERR_BAD_STATE;
    memset(ts, 0, sizeof(*ts));
    ts->migration_id = migration_id;
    return GMX_OK;
}

gmx_status_t gmx_transfer_add_stream(gmx_transfer_state_t *ts,
                                        uint32_t stream_id)
{
    if (!ts || ts->stream_count >= GMX_TRANSFER_MAX_STREAMS)
        return GMX_ERR_OVERFLOW;
    ts->stream_ids[ts->stream_count++] = stream_id;
    return GMX_OK;
}

gmx_status_t gmx_transfer_record_bytes(gmx_transfer_state_t *ts,
                                          uint32_t bytes)
{
    if (!ts) return GMX_ERR_BAD_STATE;
    ts->bytes_transferred += bytes;
    ts->frames_transferred++;
    return GMX_OK;
}

bool gmx_transfer_is_drain_complete(const gmx_transfer_state_t *ts)
{
    return ts ? ts->drain_complete : false;
}

gmx_status_t gmx_transfer_mark_drained(gmx_transfer_state_t *ts)
{
    if (!ts) return GMX_ERR_BAD_STATE;
    ts->drain_complete = true;
    return GMX_OK;
}

gmx_status_t gmx_transfer_verify_consistency(
    const gmx_transfer_state_t *ts,
    const gmx_migrate_ctx_t *ctx)
{
    if (!ts || !ctx) return GMX_ERR_BAD_STATE;
    if (ts->migration_id != ctx->migration_id) return GMX_ERR_BAD_STATE;
    return GMX_OK;
}
