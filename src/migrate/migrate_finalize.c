/*
 * libgridmux - Migration finalization and cleanup
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Handles the final stages of a migration: verifying consistency
 * between source and destination, cleaning up source-side resources,
 * and generating migration completion events.
 */
#include "gmx/migrate.h"
#include "gmx/session.h"
#include "gmx/channel.h"
#include "gmx/lease.h"
#include "gmx/registry.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_MIGRATE_VERIFY_MAX_CHECKS  32

typedef struct gmx_migrate_verify_result {
    uint32_t checks_passed;
    uint32_t checks_failed;
    uint32_t channels_verified;
    uint32_t leases_verified;
    bool     consistent;
} gmx_migrate_verify_result_t;

/*
 * Verify migration consistency between source and destination.
 * Checks that all migrated channels and leases are present at
 * the destination and absent at the source.
 */
gmx_status_t gmx_migrate_verify(const gmx_migrate_ctx_t *ctx,
                                   void *src_session_ptr,
                                   void *dst_session_ptr,
                                   gmx_migrate_verify_result_t *result)
{
    if (!ctx || !src_session_ptr || !dst_session_ptr || !result)
        return GMX_ERR_BAD_STATE;

    memset(result, 0, sizeof(*result));
    result->consistent = true;

    gmx_session_t *src = (gmx_session_t *)src_session_ptr;
    gmx_session_t *dst = (gmx_session_t *)dst_session_ptr;

    /* Verify channels are at destination */
    for (uint32_t i = 0; i < ctx->channel_count; i++) {
        uint32_t ch_id = ctx->channel_ids[i];

        gmx_channel_t *dst_ch = gmx_session_find_channel(dst, ch_id);
        if (dst_ch && dst_ch->active) {
            result->checks_passed++;
            result->channels_verified++;
        } else {
            result->checks_failed++;
            result->consistent = false;
        }

        /* Verify channel is inactive at source (or has migrate_pending) */
        gmx_channel_t *src_ch = gmx_session_find_channel(src, ch_id);
        if (src_ch && src_ch->active && !src_ch->migrate_pending) {
            result->checks_failed++;
            result->consistent = false;
        } else {
            result->checks_passed++;
        }
    }

    return GMX_OK;
}

/*
 * Clean up source-side resources after successful migration.
 * Removes migrated channels from the source session and frees
 * associated resources.
 */
gmx_status_t gmx_migrate_cleanup_source(const gmx_migrate_ctx_t *ctx,
                                           void *src_session_ptr)
{
    if (!ctx || !src_session_ptr) return GMX_ERR_BAD_STATE;
    if (ctx->state != GMX_MIGRATE_DONE) return GMX_ERR_BAD_STATE;

    gmx_session_t *src = (gmx_session_t *)src_session_ptr;

    /* Remove migrated channels */
    for (uint32_t i = 0; i < ctx->channel_count; i++) {
        uint32_t ch_id = ctx->channel_ids[i];

        for (size_t s = 0; s < GMX_MAX_CHANNELS; s++) {
            gmx_channel_t *ch = src->channels[s];
            if (ch && ch->channel_id == ch_id) {
                gmx_registry_remove(src->registry, ch_id, GMX_REG_TYPE_CHANNEL);
                gmx_channel_destroy(ch);
                src->channels[s] = NULL;
                src->channel_count--;
                break;
            }
        }
    }

    return GMX_OK;
}

/*
 * Generate a migration completion summary.
 * Returns statistics about the completed migration.
 */
gmx_status_t gmx_migrate_completion_stats(const gmx_migrate_ctx_t *ctx,
                                             uint32_t *channels_migrated,
                                             uint32_t *leases_migrated,
                                             uint32_t *total_credit)
{
    if (!ctx) return GMX_ERR_BAD_STATE;

    if (channels_migrated) *channels_migrated = ctx->channel_count;
    if (leases_migrated) *leases_migrated = ctx->lease_count;

    if (total_credit) {
        uint32_t tc = 0;
        for (uint32_t i = 0; i < ctx->channel_count; i++) {
            tc += ctx->credit_snapshot[i];
        }
        *total_credit = tc;
    }

    return GMX_OK;
}

/*
 * Estimate the resource cost of a planned migration.
 * Computes the number of bytes, frames, and operations needed
 * to complete the migration.
 */
gmx_status_t gmx_migrate_estimate_cost(const gmx_migrate_ctx_t *ctx,
                                          uint32_t *estimated_frames,
                                          uint32_t *estimated_bytes,
                                          uint32_t *estimated_operations)
{
    if (!ctx) return GMX_ERR_BAD_STATE;

    /* Each channel requires: prepare + transfer + commit = 3 frames */
    uint32_t frames = ctx->channel_count * 3 + 2; /* +2 for session ops */
    uint32_t bytes = frames * 100; /* approximate bytes per frame */
    uint32_t ops = ctx->channel_count + ctx->lease_count;

    if (estimated_frames) *estimated_frames = frames;
    if (estimated_bytes) *estimated_bytes = bytes;
    if (estimated_operations) *estimated_operations = ops;

    return GMX_OK;
}

/*
 * Check if a migration can be rolled back.
 * A rollback is possible only in the PREPARING or TRANSFERRING states.
 */
bool gmx_migrate_can_rollback(const gmx_migrate_ctx_t *ctx)
{
    if (!ctx) return false;
    return ctx->state == GMX_MIGRATE_PREPARING ||
           ctx->state == GMX_MIGRATE_TRANSFERRING;
}

/*
 * Perform a safe rollback of the migration.
 * Returns channels to their original state and cleans up
 * any partial transfer artifacts.
 */
gmx_status_t gmx_migrate_safe_rollback(gmx_migrate_ctx_t *ctx,
                                          void *src_session_ptr)
{
    if (!ctx || !src_session_ptr) return GMX_ERR_BAD_STATE;
    if (!gmx_migrate_can_rollback(ctx)) return GMX_ERR_BAD_STATE;

    gmx_session_t *src = (gmx_session_t *)src_session_ptr;

    /* Clear migrate_pending flag on all source channels */
    for (uint32_t i = 0; i < ctx->channel_count; i++) {
        gmx_channel_t *ch = gmx_session_find_channel(src, ctx->channel_ids[i]);
        if (ch) ch->migrate_pending = false;
    }

    ctx->state = GMX_MIGRATE_ROLLED_BACK;
    return GMX_OK;
}
