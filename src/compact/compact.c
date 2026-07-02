/*
 * libgridmux - Deferred compaction
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/compact.h"
#include "gmx/route.h"
#include <stdlib.h>

gmx_status_t gmx_compact_ctx_init(gmx_compact_ctx_t *ctx)
{
    if (!ctx) return GMX_ERR_BAD_STATE;
    ctx->watermark_epoch  = 0;
    ctx->sweep_generation = 0;
    ctx->entries_freed    = 0;
    ctx->entries_scanned  = 0;
    ctx->active           = false;
    return GMX_OK;
}

void gmx_compact_ctx_reset(gmx_compact_ctx_t *ctx)
{
    if (!ctx) return;
    ctx->entries_freed   = 0;
    ctx->entries_scanned = 0;
    ctx->active          = false;
}

gmx_status_t gmx_compact_route_table(gmx_compact_ctx_t *ctx,
                                      gmx_route_table_t *table,
                                      uint32_t current_epoch,
                                      uint32_t threshold)
{
    if (!ctx || !table) {
        return GMX_ERR_BAD_STATE;
    }
    if (ctx->active) {
        return GMX_ERR_COMPACT_BUSY;
    }

    ctx->active = true;
    ctx->watermark_epoch = current_epoch - threshold;
    ctx->sweep_generation++;
    ctx->entries_freed   = 0;
    ctx->entries_scanned = 0;

    for (size_t i = 0; i < table->capacity; i++) {
        gmx_route_entry_t *entry = table->entries[i];
        if (!entry) continue;

        ctx->entries_scanned++;

        /*
         * Free route entries that were created before the compaction
         * watermark epoch. Entries older than the watermark are
         * considered stale and eligible for reclamation.
         */
        if (entry->epoch_created < ctx->watermark_epoch) {
            entry->active = false;

            /* Free segment data */
            free(entry->segment_data);
            entry->segment_data = NULL;
            entry->segment_len  = 0;

            /* Free the entry itself */
            free(entry);
            table->entries[i] = NULL;
            table->count--;
            ctx->entries_freed++;
        }
    }

    ctx->active = false;
    return GMX_OK;
}

gmx_status_t gmx_compact_retire_segments(gmx_compact_ctx_t *ctx,
                                           void *segment_pool,
                                           uint32_t current_epoch)
{
    (void)segment_pool;
    (void)current_epoch;
    if (!ctx) return GMX_ERR_BAD_STATE;
    /* Stub for segment-level compaction */
    return GMX_OK;
}
