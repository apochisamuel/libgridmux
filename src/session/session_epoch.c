/*
 * libgridmux - Epoch lifecycle and history management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Maintains a rolling history of epoch transitions for consistency
 * validation, compaction watermark computation, and migration
 * synchronization. Each epoch snapshot captures registry counts,
 * route table generation, and active channel/stream counts.
 */

#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_EPOCH_HISTORY_SIZE  32

typedef struct gmx_epoch_snapshot {
    uint32_t epoch;
    uint32_t registry_count;
    uint32_t route_table_gen;
    uint32_t channel_count;
    uint32_t stream_count;
    uint32_t compact_freed;
    uint32_t sequence_at_advance;
    bool     valid;
} gmx_epoch_snapshot_t;

typedef struct gmx_epoch_history {
    gmx_epoch_snapshot_t snapshots[GMX_EPOCH_HISTORY_SIZE];
    uint32_t             write_pos;
    uint32_t             count;
    uint32_t             max_epoch_seen;
    uint32_t             min_epoch_retained;
} gmx_epoch_history_t;

gmx_status_t gmx_epoch_history_init(gmx_epoch_history_t *hist)
{
    if (!hist) return GMX_ERR_BAD_STATE;
    memset(hist, 0, sizeof(*hist));
    return GMX_OK;
}

gmx_status_t gmx_epoch_history_record(gmx_epoch_history_t *hist,
                                        uint32_t epoch,
                                        uint32_t registry_count,
                                        uint32_t route_gen,
                                        uint32_t ch_count,
                                        uint32_t st_count,
                                        uint32_t compact_freed,
                                        uint32_t sequence)
{
    if (!hist) return GMX_ERR_BAD_STATE;

    uint32_t idx = hist->write_pos % GMX_EPOCH_HISTORY_SIZE;
    gmx_epoch_snapshot_t *snap = &hist->snapshots[idx];

    snap->epoch               = epoch;
    snap->registry_count      = registry_count;
    snap->route_table_gen     = route_gen;
    snap->channel_count       = ch_count;
    snap->stream_count        = st_count;
    snap->compact_freed       = compact_freed;
    snap->sequence_at_advance = sequence;
    snap->valid               = true;

    hist->write_pos++;
    if (hist->count < GMX_EPOCH_HISTORY_SIZE) hist->count++;
    if (epoch > hist->max_epoch_seen) hist->max_epoch_seen = epoch;

    return GMX_OK;
}

gmx_status_t gmx_epoch_history_lookup(const gmx_epoch_history_t *hist,
                                        uint32_t epoch,
                                        gmx_epoch_snapshot_t *out)
{
    if (!hist || !out) return GMX_ERR_BAD_STATE;

    for (uint32_t i = 0; i < hist->count; i++) {
        const gmx_epoch_snapshot_t *snap = &hist->snapshots[i];
        if (snap->valid && snap->epoch == epoch) {
            *out = *snap;
            return GMX_OK;
        }
    }
    return GMX_ERR_NOT_FOUND;
}

/*
 * Compute the compaction watermark from epoch history.
 * Returns the oldest epoch that should be retained based on
 * the retention policy (configurable via retention_epochs param).
 */
uint32_t gmx_epoch_compute_watermark(const gmx_epoch_history_t *hist,
                                       uint32_t retention_epochs)
{
    if (!hist || hist->max_epoch_seen <= retention_epochs) return 0;
    return hist->max_epoch_seen - retention_epochs;
}

/*
 * Check if an epoch transition is monotonically valid.
 * The new epoch must be exactly one more than the current max,
 * and the sequence must have advanced sufficiently.
 */
bool gmx_epoch_is_valid_advance(const gmx_epoch_history_t *hist,
                                  uint32_t new_epoch,
                                  uint32_t current_sequence,
                                  uint32_t min_gap)
{
    if (!hist) return false;
    if (new_epoch != hist->max_epoch_seen + 1) return false;

    /* Find the last recorded epoch to check sequence gap */
    if (hist->count > 0) {
        uint32_t last_idx = (hist->write_pos - 1) % GMX_EPOCH_HISTORY_SIZE;
        const gmx_epoch_snapshot_t *last = &hist->snapshots[last_idx];
        if (last->valid) {
            if (current_sequence < last->sequence_at_advance + min_gap) {
                return false;
            }
        }
    }
    return true;
}

/*
 * Get statistics about epoch history.
 * Computes the average channel/stream count across recorded epochs,
 * which is used for capacity planning during migration.
 */
gmx_status_t gmx_epoch_history_stats(const gmx_epoch_history_t *hist,
                                       uint32_t *avg_channels,
                                       uint32_t *avg_streams,
                                       uint32_t *total_compact_freed)
{
    if (!hist || !avg_channels || !avg_streams || !total_compact_freed) {
        return GMX_ERR_BAD_STATE;
    }

    uint64_t sum_ch = 0, sum_st = 0, sum_freed = 0;
    uint32_t valid_count = 0;

    for (uint32_t i = 0; i < GMX_EPOCH_HISTORY_SIZE; i++) {
        const gmx_epoch_snapshot_t *snap = &hist->snapshots[i];
        if (snap->valid) {
            sum_ch += snap->channel_count;
            sum_st += snap->stream_count;
            sum_freed += snap->compact_freed;
            valid_count++;
        }
    }

    if (valid_count > 0) {
        *avg_channels = (uint32_t)(sum_ch / valid_count);
        *avg_streams  = (uint32_t)(sum_st / valid_count);
    } else {
        *avg_channels = 0;
        *avg_streams  = 0;
    }
    *total_compact_freed = (uint32_t)sum_freed;

    return GMX_OK;
}

/*
 * Find the epoch with the highest compaction yield.
 * Used to optimize compaction scheduling.
 */
uint32_t gmx_epoch_best_compact_epoch(const gmx_epoch_history_t *hist)
{
    if (!hist || hist->count == 0) return 0;

    uint32_t best_epoch = 0;
    uint32_t best_freed = 0;

    for (uint32_t i = 0; i < GMX_EPOCH_HISTORY_SIZE; i++) {
        const gmx_epoch_snapshot_t *snap = &hist->snapshots[i];
        if (snap->valid && snap->compact_freed > best_freed) {
            best_freed = snap->compact_freed;
            best_epoch = snap->epoch;
        }
    }
    return best_epoch;
}

/*
 * Trim epoch history to retain only the most recent N entries.
 * Older entries are invalidated.
 */
gmx_status_t gmx_epoch_history_trim(gmx_epoch_history_t *hist,
                                       uint32_t retain_count)
{
    if (!hist) return GMX_ERR_BAD_STATE;
    if (retain_count >= hist->count) return GMX_OK;

    uint32_t to_invalidate = hist->count - retain_count;
    for (uint32_t i = 0; i < GMX_EPOCH_HISTORY_SIZE && to_invalidate > 0; i++) {
        /* Invalidate oldest entries first */
        uint32_t idx = (hist->write_pos + i) % GMX_EPOCH_HISTORY_SIZE;
        if (hist->snapshots[idx].valid) {
            hist->snapshots[idx].valid = false;
            hist->count--;
            to_invalidate--;
        }
    }
    return GMX_OK;
}
