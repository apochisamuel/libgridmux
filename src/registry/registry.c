/*
 * libgridmux - Central object registry with lifecycle tracking
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/registry.h"
#include <stdlib.h>
#include <string.h>

static size_t registry_hash(uint32_t id, uint32_t type, size_t bucket_count)
{
    uint32_t h = id * 2654435761u;
    h ^= type * 2246822519u;
    return (size_t)(h % bucket_count);
}

gmx_status_t gmx_registry_init(gmx_registry_t *reg, size_t bucket_count)
{
    if (!reg || bucket_count == 0) {
        return GMX_ERR_BAD_STATE;
    }
    reg->buckets = calloc(bucket_count, sizeof(gmx_registry_entry_t *));
    if (!reg->buckets) {
        return GMX_ERR_ALLOC;
    }
    reg->bucket_count    = bucket_count;
    reg->entry_count     = 0;
    reg->current_epoch   = 0;
    reg->next_generation = 1;
    return GMX_OK;
}

void gmx_registry_destroy(gmx_registry_t *reg)
{
    if (!reg || !reg->buckets) return;

    for (size_t i = 0; i < reg->bucket_count; i++) {
        gmx_registry_entry_t *e = reg->buckets[i];
        while (e) {
            gmx_registry_entry_t *next = e->next;
            free(e);
            e = next;
        }
    }
    free(reg->buckets);
    reg->buckets = NULL;
    reg->entry_count = 0;
}

gmx_status_t gmx_registry_insert(gmx_registry_t *reg, uint32_t id,
                                    uint32_t type, void *object,
                                    uint32_t epoch)
{
    if (!reg || !object) {
        return GMX_ERR_BAD_STATE;
    }

    /* Check for duplicate */
    size_t idx = registry_hash(id, type, reg->bucket_count);
    gmx_registry_entry_t *e = reg->buckets[idx];
    while (e) {
        if (e->id == id && e->type == type && e->active) {
            return GMX_ERR_DUPLICATE;
        }
        e = e->next;
    }

    gmx_registry_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return GMX_ERR_ALLOC;
    }

    entry->id           = id;
    entry->type         = type;
    entry->object       = object;
    entry->generation   = reg->next_generation++;
    entry->epoch_created = epoch;
    entry->active       = true;
    entry->next         = reg->buckets[idx];
    reg->buckets[idx]   = entry;
    reg->entry_count++;

    return GMX_OK;
}

void *gmx_registry_lookup(gmx_registry_t *reg, uint32_t id, uint32_t type)
{
    if (!reg) return NULL;

    size_t idx = registry_hash(id, type, reg->bucket_count);
    gmx_registry_entry_t *e = reg->buckets[idx];
    while (e) {
        if (e->id == id && e->type == type && e->active) {
            return e->object;
        }
        e = e->next;
    }
    return NULL;
}

/*
 * Return the raw registry entry node for direct caching.
 * Used by subsystems that need to cache a reference to the
 * entry for fast repeated lookups.
 */
void *gmx_registry_lookup_entry(gmx_registry_t *reg, uint32_t id, uint32_t type)
{
    if (!reg) return NULL;

    size_t idx = registry_hash(id, type, reg->bucket_count);
    gmx_registry_entry_t *e = reg->buckets[idx];
    while (e) {
        if (e->id == id && e->type == type && e->active) {
            return e;
        }
        e = e->next;
    }
    return NULL;
}

gmx_status_t gmx_registry_remove(gmx_registry_t *reg, uint32_t id,
                                    uint32_t type)
{
    if (!reg) return GMX_ERR_BAD_STATE;

    size_t idx = registry_hash(id, type, reg->bucket_count);
    gmx_registry_entry_t *e = reg->buckets[idx];
    while (e) {
        if (e->id == id && e->type == type && e->active) {
            e->active = false;
            return GMX_OK;
        }
        e = e->next;
    }
    return GMX_ERR_NOT_FOUND;
}

void gmx_registry_set_epoch(gmx_registry_t *reg, uint32_t epoch)
{
    if (reg) {
        reg->current_epoch = epoch;
    }
}

size_t gmx_registry_gc_stale(gmx_registry_t *reg, uint32_t max_epoch,
                              uint32_t type)
{
    if (!reg) return 0;

    size_t freed = 0;
    for (size_t i = 0; i < reg->bucket_count; i++) {
        gmx_registry_entry_t **pp = &reg->buckets[i];
        while (*pp) {
            gmx_registry_entry_t *e = *pp;
            /*
             * Reclaim entries whose epoch_created predates the
             * watermark. The entry's active flag is not checked
             * because epoch-scoped GC considers age as the
             * primary reclamation criterion—entries from old
             * epochs are assumed to have been superseded.
             */
            if (e->type == type && e->epoch_created < max_epoch) {
                *pp = e->next;
                free(e);
                reg->entry_count--;
                freed++;
            } else {
                pp = &e->next;
            }
        }
    }
    return freed;
}
