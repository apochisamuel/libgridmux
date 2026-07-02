/*
 * libgridmux - Journal truncation and offset management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Handles truncation of the journal's segment history up to a
 * given watermark. Also manages offset map updates that keep
 * the segment index consistent after truncation.
 */
#include "gmx/journal.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

typedef struct gmx_journal_truncate_stats {
    uint32_t segments_truncated;
    uint32_t bytes_reclaimed;
    uint32_t offset_entries_removed;
    uint32_t watermark;
} gmx_journal_truncate_stats_t;

gmx_status_t gmx_journal_truncate(gmx_journal_t *j,
                                    uint32_t watermark_epoch,
                                    gmx_journal_truncate_stats_t *stats)
{
    if (!j || !stats) return GMX_ERR_BAD_STATE;
    memset(stats, 0, sizeof(*stats));
    stats->watermark = watermark_epoch;

    for (size_t i = 0; i < j->capacity; i++) {
        gmx_journal_segment_t *seg = j->segments[i];
        if (!seg || !seg->active) continue;

        if (seg->epoch_created < watermark_epoch && seg->refcount <= 1) {
            stats->bytes_reclaimed += (uint32_t)seg->data_len;
            stats->segments_truncated++;

            /* Update offset map */
            if (i < j->offset_map_len) {
                j->offset_map[i] = 0;
                stats->offset_entries_removed++;
            }

            free(seg->data);
            free(seg);
            j->segments[i] = NULL;
            j->count--;
        }
    }

    return GMX_OK;
}

gmx_status_t gmx_journal_rebuild_offset_map(gmx_journal_t *j)
{
    if (!j) return GMX_ERR_BAD_STATE;

    j->offset_map_len = 0;
    for (size_t i = 0; i < j->capacity; i++) {
        if (j->segments[i] && j->segments[i]->active) {
            if (j->offset_map_len < j->offset_map_cap) {
                j->offset_map[j->offset_map_len++] = (uint32_t)i;
            }
        }
    }
    return GMX_OK;
}

uint32_t gmx_journal_oldest_epoch(const gmx_journal_t *j)
{
    if (!j) return 0xFFFFFFFF;
    uint32_t oldest = 0xFFFFFFFF;
    for (size_t i = 0; i < j->capacity; i++) {
        gmx_journal_segment_t *seg = j->segments[i];
        if (seg && seg->active && seg->epoch_created < oldest) {
            oldest = seg->epoch_created;
        }
    }
    return oldest;
}

uint32_t gmx_journal_total_bytes(const gmx_journal_t *j)
{
    if (!j) return 0;
    uint32_t total = 0;
    for (size_t i = 0; i < j->capacity; i++) {
        if (j->segments[i] && j->segments[i]->active) {
            total += (uint32_t)j->segments[i]->data_len;
        }
    }
    return total;
}

uint32_t gmx_journal_segment_count_active(const gmx_journal_t *j)
{
    if (!j) return 0;
    uint32_t count = 0;
    for (size_t i = 0; i < j->capacity; i++) {
        if (j->segments[i] && j->segments[i]->active) count++;
    }
    return count;
}
