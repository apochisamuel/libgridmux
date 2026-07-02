/*
 * libgridmux - Registry iteration and query patterns
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/registry.h"
#include <stdlib.h>

typedef struct gmx_registry_iter {
    const gmx_registry_t *reg;
    size_t                bucket_idx;
    gmx_registry_entry_t *current;
    uint32_t              type_filter;
    bool                  active_only;
} gmx_registry_iter_t;

gmx_status_t gmx_registry_iter_init(gmx_registry_iter_t *iter,
                                       const gmx_registry_t *reg,
                                       uint32_t type_filter,
                                       bool active_only)
{
    if (!iter || !reg) return GMX_ERR_BAD_STATE;
    iter->reg = reg;
    iter->bucket_idx = 0;
    iter->current = NULL;
    iter->type_filter = type_filter;
    iter->active_only = active_only;
    return GMX_OK;
}

void *gmx_registry_iter_next(gmx_registry_iter_t *iter,
                               uint32_t *out_id, uint32_t *out_type)
{
    if (!iter || !iter->reg) return NULL;

    while (true) {
        if (iter->current) {
            iter->current = iter->current->next;
        }
        while (!iter->current) {
            if (iter->bucket_idx >= iter->reg->bucket_count) return NULL;
            iter->current = iter->reg->buckets[iter->bucket_idx];
            iter->bucket_idx++;
            if (iter->current) break;
        }
        if (!iter->current) return NULL;

        gmx_registry_entry_t *e = iter->current;
        if (iter->active_only && !e->active) continue;
        if (iter->type_filter != 0 && e->type != iter->type_filter) continue;

        if (out_id) *out_id = e->id;
        if (out_type) *out_type = e->type;
        return e->object;
    }
}

uint32_t gmx_registry_iter_count(gmx_registry_iter_t *iter)
{
    uint32_t count = 0;
    uint32_t id, type;
    gmx_registry_iter_t copy = *iter;
    while (gmx_registry_iter_next(&copy, &id, &type)) count++;
    return count;
}

void *gmx_registry_find_by_generation(const gmx_registry_t *reg,
                                        uint32_t min_generation,
                                        uint32_t max_generation,
                                        uint32_t *out_id)
{
    if (!reg) return NULL;
    for (size_t i = 0; i < reg->bucket_count; i++) {
        gmx_registry_entry_t *e = reg->buckets[i];
        while (e) {
            if (e->active && e->generation >= min_generation &&
                e->generation <= max_generation) {
                if (out_id) *out_id = e->id;
                return e->object;
            }
            e = e->next;
        }
    }
    return NULL;
}

void *gmx_registry_find_by_epoch_range(const gmx_registry_t *reg,
                                         uint32_t min_epoch,
                                         uint32_t max_epoch,
                                         uint32_t type,
                                         uint32_t *out_id)
{
    if (!reg) return NULL;
    for (size_t i = 0; i < reg->bucket_count; i++) {
        gmx_registry_entry_t *e = reg->buckets[i];
        while (e) {
            if (e->active && e->epoch_created >= min_epoch &&
                e->epoch_created <= max_epoch &&
                (type == 0 || e->type == type)) {
                if (out_id) *out_id = e->id;
                return e->object;
            }
            e = e->next;
        }
    }
    return NULL;
}

size_t gmx_registry_collect_ids(const gmx_registry_t *reg,
                                  uint32_t type, bool active_only,
                                  uint32_t *ids, size_t max_ids)
{
    if (!reg || !ids || max_ids == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < reg->bucket_count && count < max_ids; i++) {
        gmx_registry_entry_t *e = reg->buckets[i];
        while (e && count < max_ids) {
            if ((!active_only || e->active) &&
                (type == 0 || e->type == type)) {
                ids[count++] = e->id;
            }
            e = e->next;
        }
    }
    return count;
}
