/*
 * libgridmux - Epoch-scoped lease operations
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Lease operations that interact with epoch boundaries: scanning
 * for expired leases, extending lease duration across epoch
 * transitions, and collecting lease statistics for compaction.
 */
#include "gmx/lease.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

typedef struct gmx_lease_epoch_stats {
    uint32_t total_leases;
    uint32_t active_leases;
    uint32_t expired_leases;
    uint32_t extended_leases;
    uint32_t total_refcount;
    uint32_t avg_history_depth;
} gmx_lease_epoch_stats_t;

gmx_status_t gmx_lease_scan_epoch(gmx_lease_table_t *lt,
                                    uint32_t current_epoch,
                                    uint32_t max_age,
                                    gmx_lease_epoch_stats_t *stats)
{
    if (!lt || !stats) return GMX_ERR_BAD_STATE;
    memset(stats, 0, sizeof(*stats));

    uint64_t total_hist = 0;
    uint32_t hist_count = 0;

    for (size_t i = 0; i < lt->capacity; i++) {
        gmx_lease_t *l = lt->entries[i];
        if (!l) continue;

        stats->total_leases++;

        if (l->active) {
            stats->active_leases++;
            stats->total_refcount += l->refcount;

            if (l->extended) {
                stats->extended_leases++;
                total_hist += l->history_depth;
                hist_count++;
            }

            if (current_epoch - l->epoch_acquired > max_age) {
                stats->expired_leases++;
            }
        }
    }

    if (hist_count > 0) {
        stats->avg_history_depth = (uint32_t)(total_hist / hist_count);
    }

    return GMX_OK;
}

uint32_t gmx_lease_count_expired(gmx_lease_table_t *lt,
                                   uint32_t current_epoch,
                                   uint32_t max_age)
{
    if (!lt) return 0;
    uint32_t count = 0;
    for (size_t i = 0; i < lt->capacity; i++) {
        gmx_lease_t *l = lt->entries[i];
        if (l && l->active && current_epoch - l->epoch_acquired > max_age) {
            count++;
        }
    }
    return count;
}

gmx_status_t gmx_lease_expire_old(gmx_lease_table_t *lt,
                                     uint32_t current_epoch,
                                     uint32_t max_age,
                                     uint32_t *expired_count)
{
    if (!lt || !expired_count) return GMX_ERR_BAD_STATE;
    *expired_count = 0;

    for (size_t i = 0; i < lt->capacity; i++) {
        gmx_lease_t *l = lt->entries[i];
        if (!l || !l->active) continue;

        if (current_epoch - l->epoch_acquired > max_age &&
            l->refcount <= 1) {
            l->active = false;
            (*expired_count)++;
        }
    }
    return GMX_OK;
}

gmx_status_t gmx_lease_extend_all(gmx_lease_table_t *lt,
                                     uint32_t extension_amount)
{
    if (!lt || extension_amount == 0) return GMX_ERR_BAD_STATE;

    for (size_t i = 0; i < lt->capacity; i++) {
        gmx_lease_t *l = lt->entries[i];
        if (l && l->active && !l->extended) {
            l->history_depth += extension_amount;
            l->extended = true;
        }
    }
    return GMX_OK;
}

size_t gmx_lease_collect_active_ids(gmx_lease_table_t *lt,
                                      uint32_t *ids, size_t max_ids)
{
    if (!lt || !ids || max_ids == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < lt->capacity && count < max_ids; i++) {
        gmx_lease_t *l = lt->entries[i];
        if (l && l->active) {
            ids[count++] = l->lease_id;
        }
    }
    return count;
}
