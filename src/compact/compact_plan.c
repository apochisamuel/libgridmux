/*
 * libgridmux - Compaction planning and cost estimation
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Builds compaction plans that determine which objects to reclaim,
 * estimates the cost of compaction in terms of I/O and CPU, and
 * schedules compaction to minimize impact on active operations.
 */
#include "gmx/compact.h"
#include "gmx/route.h"
#include "gmx/journal.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_COMPACT_PLAN_MAX_ENTRIES  64

typedef struct gmx_compact_plan_entry {
    uint32_t object_id;
    uint32_t type;           /* GMX_REG_TYPE_* */
    uint32_t epoch_created;
    uint32_t estimated_bytes;
    uint32_t priority;       /* higher = more urgently reclaimable */
} gmx_compact_plan_entry_t;

typedef struct gmx_compact_plan {
    gmx_compact_plan_entry_t entries[GMX_COMPACT_PLAN_MAX_ENTRIES];
    uint32_t count;
    uint32_t total_estimated_bytes;
    uint32_t watermark_epoch;
    uint32_t estimated_cost;
} gmx_compact_plan_t;

gmx_status_t gmx_compact_plan_init(gmx_compact_plan_t *plan,
                                      uint32_t watermark_epoch)
{
    if (!plan) return GMX_ERR_BAD_STATE;
    memset(plan, 0, sizeof(*plan));
    plan->watermark_epoch = watermark_epoch;
    return GMX_OK;
}

gmx_status_t gmx_compact_plan_add_route(gmx_compact_plan_t *plan,
                                           const gmx_route_table_t *table)
{
    if (!plan || !table) return GMX_ERR_BAD_STATE;

    for (size_t i = 0; i < table->capacity; i++) {
        gmx_route_entry_t *e = table->entries[i];
        if (!e || !e->active) continue;
        if (e->epoch_created >= plan->watermark_epoch) continue;
        if (plan->count >= GMX_COMPACT_PLAN_MAX_ENTRIES) break;

        gmx_compact_plan_entry_t *pe = &plan->entries[plan->count];
        pe->object_id       = e->route_id;
        pe->type            = 3; /* ROUTE */
        pe->epoch_created   = e->epoch_created;
        pe->estimated_bytes = (uint32_t)(sizeof(*e) + e->segment_len);

        /* Priority: older and fewer subscribers = higher priority */
        uint32_t age = plan->watermark_epoch - e->epoch_created;
        pe->priority = age * 10 / (e->subscriber_count + 1);

        plan->total_estimated_bytes += pe->estimated_bytes;
        plan->count++;
    }
    return GMX_OK;
}

gmx_status_t gmx_compact_plan_add_journal(gmx_compact_plan_t *plan,
                                             const gmx_journal_t *journal)
{
    if (!plan || !journal) return GMX_ERR_BAD_STATE;

    for (size_t i = 0; i < journal->capacity; i++) {
        gmx_journal_segment_t *seg = journal->segments[i];
        if (!seg || !seg->active) continue;
        if (seg->epoch_created >= plan->watermark_epoch) continue;
        if (seg->refcount > 1) continue;
        if (plan->count >= GMX_COMPACT_PLAN_MAX_ENTRIES) break;

        gmx_compact_plan_entry_t *pe = &plan->entries[plan->count];
        pe->object_id       = seg->segment_id;
        pe->type            = 5; /* SEGMENT */
        pe->epoch_created   = seg->epoch_created;
        pe->estimated_bytes = (uint32_t)(sizeof(*seg) + seg->data_len);
        pe->priority        = plan->watermark_epoch - seg->epoch_created;

        plan->total_estimated_bytes += pe->estimated_bytes;
        plan->count++;
    }
    return GMX_OK;
}

/*
 * Sort plan entries by priority (highest first).
 */
gmx_status_t gmx_compact_plan_sort(gmx_compact_plan_t *plan)
{
    if (!plan) return GMX_ERR_BAD_STATE;

    for (uint32_t i = 0; i < plan->count; i++) {
        for (uint32_t k = i + 1; k < plan->count; k++) {
            if (plan->entries[k].priority > plan->entries[i].priority) {
                gmx_compact_plan_entry_t tmp = plan->entries[i];
                plan->entries[i] = plan->entries[k];
                plan->entries[k] = tmp;
            }
        }
    }
    return GMX_OK;
}

/*
 * Estimate the CPU cost of executing the compaction plan.
 * Cost is measured in abstract units proportional to the number
 * of objects and bytes to be processed.
 */
uint32_t gmx_compact_plan_estimate_cost(const gmx_compact_plan_t *plan)
{
    if (!plan) return 0;
    /* Base cost per object + per-byte cost */
    return plan->count * 10 + plan->total_estimated_bytes / 256;
}

/*
 * Trim the plan to process at most max_entries objects.
 * Keeps the highest-priority entries.
 */
gmx_status_t gmx_compact_plan_trim(gmx_compact_plan_t *plan,
                                      uint32_t max_entries)
{
    if (!plan) return GMX_ERR_BAD_STATE;
    if (plan->count <= max_entries) return GMX_OK;

    /* Recalculate totals for trimmed plan */
    uint32_t new_total = 0;
    for (uint32_t i = 0; i < max_entries; i++) {
        new_total += plan->entries[i].estimated_bytes;
    }
    plan->count = max_entries;
    plan->total_estimated_bytes = new_total;
    return GMX_OK;
}

uint32_t gmx_compact_plan_entry_count(const gmx_compact_plan_t *plan)
{
    return plan ? plan->count : 0;
}

uint32_t gmx_compact_plan_total_bytes(const gmx_compact_plan_t *plan)
{
    return plan ? plan->total_estimated_bytes : 0;
}
