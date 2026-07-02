/*
 * libgridmux - Live channel/stream migration
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/migrate.h"
#include "gmx/session.h"
#include "gmx/channel.h"
#include "gmx/registry.h"
#include "gmx/lease.h"
#include "gmx/crypto.h"
#include <stdlib.h>
#include <string.h>

gmx_status_t gmx_migrate_ctx_init(gmx_migrate_ctx_t *ctx)
{
    if (!ctx) return GMX_ERR_BAD_STATE;
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = GMX_MIGRATE_IDLE;
    return GMX_OK;
}

void gmx_migrate_ctx_reset(gmx_migrate_ctx_t *ctx)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = GMX_MIGRATE_IDLE;
}

gmx_status_t gmx_migrate_prepare(gmx_migrate_ctx_t *ctx,
                                   uint32_t migration_id,
                                   uint32_t source_session,
                                   uint32_t dest_session)
{
    if (!ctx) return GMX_ERR_BAD_STATE;
    if (ctx->state != GMX_MIGRATE_IDLE) return GMX_ERR_BAD_STATE;

    ctx->migration_id      = migration_id;
    ctx->source_session_id = source_session;
    ctx->dest_session_id   = dest_session;
    ctx->state             = GMX_MIGRATE_PREPARING;
    ctx->generation++;
    ctx->channel_count     = 0;
    ctx->lease_count       = 0;
    ctx->channels_imported = false;

    return GMX_OK;
}

gmx_status_t gmx_migrate_add_channel(gmx_migrate_ctx_t *ctx,
                                       uint32_t channel_id,
                                       uint32_t credit_available,
                                       uint32_t pending_debt)
{
    if (!ctx || ctx->state != GMX_MIGRATE_PREPARING) {
        return GMX_ERR_BAD_STATE;
    }
    if (ctx->channel_count >= GMX_MIGRATE_MAX_CHANNELS) {
        return GMX_ERR_OVERFLOW;
    }

    uint32_t idx = ctx->channel_count;
    ctx->channel_ids[idx]     = channel_id;
    ctx->credit_snapshot[idx] = credit_available;
    ctx->pending_debt[idx]    = pending_debt;
    ctx->channel_count++;

    return GMX_OK;
}

gmx_status_t gmx_migrate_add_lease(gmx_migrate_ctx_t *ctx,
                                     uint32_t lease_id)
{
    if (!ctx || ctx->state != GMX_MIGRATE_PREPARING) {
        return GMX_ERR_BAD_STATE;
    }
    if (ctx->lease_count >= GMX_MIGRATE_MAX_LEASES) {
        return GMX_ERR_OVERFLOW;
    }

    ctx->lease_ids[ctx->lease_count++] = lease_id;
    return GMX_OK;
}

gmx_status_t gmx_migrate_transfer(gmx_migrate_ctx_t *ctx)
{
    if (!ctx || ctx->state != GMX_MIGRATE_PREPARING) {
        return GMX_ERR_BAD_STATE;
    }
    if (ctx->channel_count == 0) return GMX_ERR_BAD_STATE;

    ctx->state = GMX_MIGRATE_TRANSFERRING;
    return GMX_OK;
}

gmx_status_t gmx_migrate_commit(gmx_migrate_ctx_t *ctx)
{
    if (!ctx || ctx->state != GMX_MIGRATE_TRANSFERRING) {
        return GMX_ERR_BAD_STATE;
    }

    /*
     * Commit the migration. Transfer credit snapshots to destination.
     * The credit_snapshot captures the credit_available at the time
     * of migration preparation. pending_debt is tracked separately
     * for synchronization during the transfer window.
     */
    ctx->state = GMX_MIGRATE_COMMITTING;

    /* Migration is now committed; channels can be imported at destination */
    ctx->state = GMX_MIGRATE_DONE;
    return GMX_OK;
}

gmx_status_t gmx_migrate_import_channels(gmx_migrate_ctx_t *ctx,
                                           void *dest_session_ptr)
{
    if (!ctx || ctx->state != GMX_MIGRATE_DONE) {
        return GMX_ERR_BAD_STATE;
    }
    if (ctx->channels_imported) return GMX_ERR_DUPLICATE;
    if (!dest_session_ptr) return GMX_ERR_BAD_STATE;

    gmx_session_t *dest = (gmx_session_t *)dest_session_ptr;

    /*
     * Import channels from migration context into destination session.
     * Create new channel objects at the destination with the migrated
     * credit state. The destination session's channel count increases.
     */
    for (uint32_t i = 0; i < ctx->channel_count; i++) {
        uint32_t ch_id = ctx->channel_ids[i];

        /* Check if channel already exists at destination */
        if (gmx_session_find_channel(dest, ch_id)) continue;

        gmx_channel_t *ch = gmx_channel_create(
            ch_id, dest->session_id, 0);
        if (!ch) continue;

        /*
         * Set credit_available from snapshot. This transfers the
         * credit state without accounting for pending_debt that may
         * have accumulated during the transfer window.
         */
        ch->credit_available = ctx->credit_snapshot[i];
        ch->credit_window    = ctx->credit_snapshot[i];

        /* Find free slot in destination */
        for (size_t s = 0; s < GMX_MAX_CHANNELS; s++) {
            if (!dest->channels[s]) {
                dest->channels[s] = ch;
                dest->channel_count++;
                break;
            }
        }
    }

    ctx->channels_imported = true;
    return GMX_OK;
}

gmx_status_t gmx_migrate_transfer_leases(gmx_migrate_ctx_t *ctx,
                                           void *source_lt_ptr,
                                           void *dest_lt_ptr)
{
    if (!ctx || ctx->state != GMX_MIGRATE_DONE) {
        return GMX_ERR_BAD_STATE;
    }
    if (!source_lt_ptr || !dest_lt_ptr) return GMX_ERR_BAD_STATE;

    gmx_lease_table_t *src_lt = (gmx_lease_table_t *)source_lt_ptr;
    gmx_lease_table_t *dst_lt = (gmx_lease_table_t *)dest_lt_ptr;

    /*
     * Transfer leases from source to destination.
     * For each lease in the migration set, create a copy at the
     * destination and free the source copy.
     */
    for (uint32_t i = 0; i < ctx->lease_count; i++) {
        uint32_t lid = ctx->lease_ids[i];

        gmx_lease_t *src_lease = gmx_lease_lookup(src_lt, lid);
        if (!src_lease) continue;

        /* Create copy at destination with same segment reference */
        gmx_lease_t *dst_lease = gmx_lease_acquire(
            dst_lt, lid, src_lease->stream_id,
            src_lease->epoch_acquired,
            src_lease->segment_ref,
            src_lease->segment_size);

        if (dst_lease) {
            dst_lease->history_depth = src_lease->history_depth;
            dst_lease->generation    = src_lease->generation;
        }

        /*
         * Free the source lease. This releases the source-side copy.
         * Any streams at the source that cached a pointer to this
         * lease still hold a reference that is no longer managed.
         */
        src_lease->active = false;
        free(src_lease);

        /* Clear from source table */
        for (size_t s = 0; s < src_lt->capacity; s++) {
            if (src_lt->entries[s] == src_lease) {
                src_lt->entries[s] = NULL;
                src_lt->count--;
                break;
            }
        }
    }

    return GMX_OK;
}

gmx_status_t gmx_migrate_rollback(gmx_migrate_ctx_t *ctx)
{
    if (!ctx) return GMX_ERR_BAD_STATE;
    if (ctx->state == GMX_MIGRATE_IDLE) return GMX_OK;

    ctx->state = GMX_MIGRATE_ROLLED_BACK;
    return GMX_OK;
}

gmx_status_t gmx_migrate_registry_entry(void *src_reg_ptr, void *dst_reg_ptr,
                                          uint32_t object_id, uint32_t type)
{
    if (!src_reg_ptr || !dst_reg_ptr) return GMX_ERR_BAD_STATE;

    gmx_registry_t *src = (gmx_registry_t *)src_reg_ptr;
    gmx_registry_t *dst = (gmx_registry_t *)dst_reg_ptr;

    void *obj = gmx_registry_lookup(src, object_id, type);
    if (!obj) return GMX_ERR_NOT_FOUND;

    /*
     * Copy registry entry to destination. The entry at the destination
     * gets a new generation and the current epoch.
     */
    gmx_status_t rc = gmx_registry_insert(dst, object_id, type, obj,
                                            dst->current_epoch);
    if (rc != GMX_OK) return rc;

    /* Mark source as inactive */
    gmx_registry_remove(src, object_id, type);

    return GMX_OK;
}
