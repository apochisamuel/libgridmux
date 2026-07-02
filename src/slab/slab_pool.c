/*
 * libgridmux - Slab pool management and memory statistics
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/slab.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_SLAB_MAX_POOLS  16

typedef struct gmx_slab_pool_registry {
    gmx_slab_pool_t *pools[GMX_SLAB_MAX_POOLS];
    const char      *pool_names[GMX_SLAB_MAX_POOLS];
    size_t           count;
} gmx_slab_pool_registry_t;

gmx_status_t gmx_slab_registry_init(gmx_slab_pool_registry_t *reg)
{
    if (!reg) return GMX_ERR_BAD_STATE;
    memset(reg, 0, sizeof(*reg));
    return GMX_OK;
}

gmx_status_t gmx_slab_registry_add(gmx_slab_pool_registry_t *reg,
                                      gmx_slab_pool_t *pool,
                                      const char *name)
{
    if (!reg || !pool || reg->count >= GMX_SLAB_MAX_POOLS)
        return GMX_ERR_OVERFLOW;
    reg->pools[reg->count] = pool;
    reg->pool_names[reg->count] = name;
    reg->count++;
    return GMX_OK;
}

gmx_slab_pool_t *gmx_slab_registry_find(gmx_slab_pool_registry_t *reg,
                                           const char *name)
{
    if (!reg || !name) return NULL;
    for (size_t i = 0; i < reg->count; i++) {
        if (reg->pool_names[i] && strcmp(reg->pool_names[i], name) == 0)
            return reg->pools[i];
    }
    return NULL;
}

typedef struct gmx_slab_stats {
    size_t total_allocs;
    size_t total_frees;
    size_t pool_count;
    size_t total_object_bytes;
    size_t free_list_bytes;
} gmx_slab_stats_t;

gmx_status_t gmx_slab_registry_stats(const gmx_slab_pool_registry_t *reg,
                                        gmx_slab_stats_t *stats)
{
    if (!reg || !stats) return GMX_ERR_BAD_STATE;
    memset(stats, 0, sizeof(*stats));
    stats->pool_count = reg->count;

    for (size_t i = 0; i < reg->count; i++) {
        gmx_slab_pool_t *p = reg->pools[i];
        if (p) {
            stats->total_allocs += p->total_allocs;
            stats->total_frees += p->total_frees;
            size_t live = p->total_allocs - p->total_frees;
            stats->total_object_bytes += live * p->object_size;
            stats->free_list_bytes += p->free_count * p->object_size;
        }
    }
    return GMX_OK;
}

void gmx_slab_registry_destroy(gmx_slab_pool_registry_t *reg)
{
    if (!reg) return;
    for (size_t i = 0; i < reg->count; i++) {
        if (reg->pools[i]) {
            gmx_slab_pool_destroy(reg->pools[i]);
        }
    }
    memset(reg, 0, sizeof(*reg));
}
