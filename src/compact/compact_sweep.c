/*
 * libgridmux - Segment-level compaction sweep
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/compact.h"
#include "gmx/journal.h"
#include "gmx/registry.h"
#include <string.h>
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>

typedef struct gmx_compact_segment_stats {
    uint32_t total_segments;
    uint32_t retired_segments;
    uint32_t bytes_reclaimed;
    uint32_t watermark;
} gmx_compact_segment_stats_t;

gmx_status_t gmx_compact_sweep_journal(
    gmx_compact_ctx_t *ctx,
    gmx_journal_t *journal,
    uint32_t current_epoch,
    uint32_t retention_epochs,
    gmx_compact_segment_stats_t *stats)
{
    if (!ctx || !journal || !stats) return GMX_ERR_BAD_STATE;
    if (ctx->active) return GMX_ERR_COMPACT_BUSY;

    ctx->active = true;
    memset(stats, 0, sizeof(*stats));
    stats->watermark = current_epoch > retention_epochs ?
                       current_epoch - retention_epochs : 0;

    for (size_t i = 0; i < journal->capacity; i++) {
        gmx_journal_segment_t *seg = journal->segments[i];
        if (!seg) continue;

        stats->total_segments++;

        if (seg->epoch_created < stats->watermark &&
            seg->refcount <= 1 && seg->sealed) {
            stats->bytes_reclaimed += (uint32_t)seg->data_len;
            seg->active = false;
            stats->retired_segments++;
        }
    }

    ctx->active = false;
    return GMX_OK;
}

gmx_status_t gmx_compact_reclaim_segments(
    gmx_journal_t *journal)
{
    if (!journal) return GMX_ERR_BAD_STATE;

    for (size_t i = 0; i < journal->capacity; i++) {
        gmx_journal_segment_t *seg = journal->segments[i];
        if (seg && !seg->active) {
            free(seg->data);
            free(seg);
            journal->segments[i] = NULL;
            journal->count--;
        }
    }
    return GMX_OK;
}

uint32_t gmx_compact_estimate_reclaimable(
    const gmx_journal_t *journal,
    uint32_t watermark_epoch)
{
    if (!journal) return 0;
    uint32_t bytes = 0;
    for (size_t i = 0; i < journal->capacity; i++) {
        gmx_journal_segment_t *seg = journal->segments[i];
        if (seg && seg->active && seg->epoch_created < watermark_epoch &&
            seg->refcount <= 1) {
            bytes += (uint32_t)seg->data_len;
        }
    }
    return bytes;
}

bool gmx_compact_should_run(const gmx_journal_t *journal,
                              uint32_t threshold_bytes)
{
    if (!journal) return false;
    uint32_t total = 0;
    for (size_t i = 0; i < journal->capacity; i++) {
        if (journal->segments[i] && journal->segments[i]->active) {
            total += (uint32_t)journal->segments[i]->data_len;
        }
    }
    return total >= threshold_bytes;
}
