/*
 * libgridmux - Quota enforcement and accounting engine
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Implements the quota enforcement layer that sits between channel
 * operations and the underlying resource pools. Provides fair-share
 * scheduling, burst allowance computation, and cross-group accounting
 * for multi-tenant deployments.
 */
#include "gmx/quota.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_QUOTA_BURST_FACTOR    2
#define GMX_QUOTA_HISTORY_SIZE    16

typedef struct gmx_quota_enforcement_entry {
    uint32_t timestamp;
    uint32_t amount;
    uint32_t group_id;
    bool     allowed;
} gmx_quota_enforcement_entry_t;

typedef struct gmx_quota_enforcer {
    gmx_quota_table_t  *table;
    uint32_t            total_consumed_global;
    uint32_t            total_rejected;
    uint32_t            burst_allowance;
    gmx_quota_enforcement_entry_t history[GMX_QUOTA_HISTORY_SIZE];
    uint32_t            hist_pos;
    uint32_t            hist_count;
    uint32_t            window_start_epoch;
    uint32_t            window_consumed;
} gmx_quota_enforcer_t;

gmx_status_t gmx_quota_enforcer_init(gmx_quota_enforcer_t *enf,
                                        gmx_quota_table_t *table)
{
    if (!enf || !table) return GMX_ERR_BAD_STATE;
    memset(enf, 0, sizeof(*enf));
    enf->table = table;
    enf->burst_allowance = GMX_QUOTA_BURST_FACTOR;
    return GMX_OK;
}

/*
 * Check if a consumption request is allowed under the quota policy.
 * Returns GMX_OK if allowed, GMX_ERR_OVERFLOW if quota exceeded.
 */
gmx_status_t gmx_quota_enforce_check(gmx_quota_enforcer_t *enf,
                                        uint32_t group_id,
                                        uint32_t amount)
{
    if (!enf || !enf->table) return GMX_ERR_BAD_STATE;

    gmx_quota_bucket_t *bucket = gmx_quota_lookup_bucket(enf->table, group_id);
    if (!bucket || !bucket->active) return GMX_ERR_NOT_FOUND;

    /* Compute effective limit with burst allowance */
    uint32_t effective_limit = bucket->limit;
    if (bucket->active_count > 0 && bucket->active_count <= 4) {
        effective_limit = bucket->limit * enf->burst_allowance;
    }

    /* Check against effective limit */
    if (bucket->consumed + amount > effective_limit) {
        enf->total_rejected++;
        return GMX_ERR_OVERFLOW;
    }

    return GMX_OK;
}

/*
 * Execute a quota consumption with enforcement and history recording.
 */
gmx_status_t gmx_quota_enforce_consume(gmx_quota_enforcer_t *enf,
                                          uint32_t group_id,
                                          uint32_t amount,
                                          uint32_t timestamp)
{
    if (!enf) return GMX_ERR_BAD_STATE;

    gmx_status_t rc = gmx_quota_enforce_check(enf, group_id, amount);
    if (rc != GMX_OK) {
        /* Record rejection in history */
        uint32_t idx = enf->hist_pos % GMX_QUOTA_HISTORY_SIZE;
        enf->history[idx].timestamp = timestamp;
        enf->history[idx].amount = amount;
        enf->history[idx].group_id = group_id;
        enf->history[idx].allowed = false;
        enf->hist_pos++;
        if (enf->hist_count < GMX_QUOTA_HISTORY_SIZE) enf->hist_count++;
        return rc;
    }

    /* Perform actual consumption */
    gmx_quota_bucket_t *bucket = gmx_quota_lookup_bucket(enf->table, group_id);
    rc = gmx_quota_consume(bucket, amount);
    if (rc != GMX_OK) return rc;

    enf->total_consumed_global += amount;
    enf->window_consumed += amount;

    /* Record in history */
    uint32_t idx = enf->hist_pos % GMX_QUOTA_HISTORY_SIZE;
    enf->history[idx].timestamp = timestamp;
    enf->history[idx].amount = amount;
    enf->history[idx].group_id = group_id;
    enf->history[idx].allowed = true;
    enf->hist_pos++;
    if (enf->hist_count < GMX_QUOTA_HISTORY_SIZE) enf->hist_count++;

    return GMX_OK;
}

/*
 * Compute the fair-share allocation for a group based on the
 * number of active groups and the global resource pool.
 */
uint32_t gmx_quota_compute_fair_share(const gmx_quota_enforcer_t *enf,
                                        uint32_t group_id,
                                        uint32_t total_capacity)
{
    if (!enf || !enf->table || enf->table->count == 0) return 0;

    gmx_quota_bucket_t *bucket = gmx_quota_lookup_bucket(
        (gmx_quota_table_t *)enf->table, group_id);
    if (!bucket) return 0;

    /* Base share: total capacity divided by active groups */
    uint32_t base_share = total_capacity / (uint32_t)enf->table->count;

    /* Weight by the group's member count relative to others */
    if (bucket->active_count > 0) {
        uint32_t weight_factor = bucket->active_count;
        if (weight_factor > 8) weight_factor = 8;
        base_share = (base_share * weight_factor) / 4;
    }

    return base_share;
}

/*
 * Reset the enforcement window for a new epoch.
 * Preserves history but resets per-window counters.
 */
gmx_status_t gmx_quota_enforce_reset_window(gmx_quota_enforcer_t *enf,
                                               uint32_t new_epoch)
{
    if (!enf) return GMX_ERR_BAD_STATE;
    enf->window_start_epoch = new_epoch;
    enf->window_consumed = 0;
    return GMX_OK;
}

/*
 * Get enforcement statistics.
 */
gmx_status_t gmx_quota_enforce_stats(const gmx_quota_enforcer_t *enf,
                                        uint32_t *total_consumed,
                                        uint32_t *total_rejected,
                                        uint32_t *window_consumed)
{
    if (!enf) return GMX_ERR_BAD_STATE;
    if (total_consumed) *total_consumed = enf->total_consumed_global;
    if (total_rejected) *total_rejected = enf->total_rejected;
    if (window_consumed) *window_consumed = enf->window_consumed;
    return GMX_OK;
}

/*
 * Scan history for a specific group's consumption pattern.
 * Returns the total consumed in the most recent N entries.
 */
uint32_t gmx_quota_enforce_recent_consumption(
    const gmx_quota_enforcer_t *enf,
    uint32_t group_id,
    uint32_t max_entries)
{
    if (!enf) return 0;
    uint32_t total = 0;
    uint32_t scanned = 0;

    for (uint32_t i = 0; i < enf->hist_count && scanned < max_entries; i++) {
        uint32_t idx = (enf->hist_pos - 1 - i) % GMX_QUOTA_HISTORY_SIZE;
        if (enf->history[idx].group_id == group_id &&
            enf->history[idx].allowed) {
            total += enf->history[idx].amount;
            scanned++;
        }
    }
    return total;
}
