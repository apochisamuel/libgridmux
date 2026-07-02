/*
 * libgridmux - Session keepalive and health monitoring
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_HEARTBEAT_HISTORY  16
#define GMX_HEARTBEAT_TIMEOUT  30

typedef struct gmx_heartbeat_entry {
    uint32_t sequence;
    uint32_t epoch;
    uint32_t timestamp;
    uint32_t rtt_estimate;
    bool     acked;
} gmx_heartbeat_entry_t;

typedef struct gmx_heartbeat_ctx {
    gmx_heartbeat_entry_t history[GMX_HEARTBEAT_HISTORY];
    uint32_t write_pos;
    uint32_t count;
    uint32_t avg_rtt;
    uint32_t min_rtt;
    uint32_t max_rtt;
    uint32_t consecutive_missed;
    uint32_t last_recv_epoch;
    bool     healthy;
} gmx_heartbeat_ctx_t;

gmx_status_t gmx_heartbeat_init(gmx_heartbeat_ctx_t *ctx)
{
    if (!ctx) return GMX_ERR_BAD_STATE;
    memset(ctx, 0, sizeof(*ctx));
    ctx->healthy = true;
    ctx->min_rtt = 0xFFFFFFFF;
    return GMX_OK;
}

gmx_status_t gmx_heartbeat_send(gmx_heartbeat_ctx_t *ctx,
                                  uint32_t sequence, uint32_t epoch,
                                  uint32_t timestamp)
{
    if (!ctx) return GMX_ERR_BAD_STATE;
    uint32_t idx = ctx->write_pos % GMX_HEARTBEAT_HISTORY;
    ctx->history[idx].sequence = sequence;
    ctx->history[idx].epoch = epoch;
    ctx->history[idx].timestamp = timestamp;
    ctx->history[idx].rtt_estimate = 0;
    ctx->history[idx].acked = false;
    ctx->write_pos++;
    if (ctx->count < GMX_HEARTBEAT_HISTORY) ctx->count++;
    return GMX_OK;
}

gmx_status_t gmx_heartbeat_recv(gmx_heartbeat_ctx_t *ctx,
                                  uint32_t sequence,
                                  uint32_t recv_timestamp)
{
    if (!ctx) return GMX_ERR_BAD_STATE;

    for (uint32_t i = 0; i < GMX_HEARTBEAT_HISTORY; i++) {
        gmx_heartbeat_entry_t *e = &ctx->history[i];
        if (e->sequence == sequence && !e->acked) {
            e->acked = true;
            e->rtt_estimate = recv_timestamp - e->timestamp;
            ctx->consecutive_missed = 0;
            ctx->last_recv_epoch = e->epoch;

            /* Update RTT statistics */
            if (e->rtt_estimate < ctx->min_rtt) ctx->min_rtt = e->rtt_estimate;
            if (e->rtt_estimate > ctx->max_rtt) ctx->max_rtt = e->rtt_estimate;

            uint64_t sum = 0;
            uint32_t rtt_count = 0;
            for (uint32_t j = 0; j < GMX_HEARTBEAT_HISTORY; j++) {
                if (ctx->history[j].acked && ctx->history[j].rtt_estimate > 0) {
                    sum += ctx->history[j].rtt_estimate;
                    rtt_count++;
                }
            }
            if (rtt_count > 0) ctx->avg_rtt = (uint32_t)(sum / rtt_count);

            return GMX_OK;
        }
    }
    return GMX_ERR_NOT_FOUND;
}

gmx_status_t gmx_heartbeat_check_timeout(gmx_heartbeat_ctx_t *ctx,
                                            uint32_t current_timestamp)
{
    if (!ctx) return GMX_ERR_BAD_STATE;

    uint32_t missed = 0;
    for (uint32_t i = 0; i < ctx->count; i++) {
        gmx_heartbeat_entry_t *e = &ctx->history[i];
        if (!e->acked && (current_timestamp - e->timestamp) > GMX_HEARTBEAT_TIMEOUT) {
            missed++;
        }
    }
    ctx->consecutive_missed = missed;
    ctx->healthy = (missed < 3);
    return GMX_OK;
}

bool gmx_heartbeat_is_healthy(const gmx_heartbeat_ctx_t *ctx)
{
    return ctx ? ctx->healthy : false;
}

uint32_t gmx_heartbeat_avg_rtt(const gmx_heartbeat_ctx_t *ctx)
{
    return ctx ? ctx->avg_rtt : 0;
}
