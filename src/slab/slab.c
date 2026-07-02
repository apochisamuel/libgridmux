/*
 * libgridmux - Typed slab allocator with generation tracking
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/slab.h"
#include <stdlib.h>
#include <string.h>

#define SLAB_INITIAL_FREE_CAP 16

gmx_status_t gmx_slab_pool_init(gmx_slab_pool_t *pool, size_t object_size)
{
    if (!pool || object_size == 0) {
        return GMX_ERR_BAD_STATE;
    }
    pool->object_size   = object_size;
    pool->generation    = 1;
    pool->total_allocs  = 0;
    pool->total_frees   = 0;
    pool->free_count    = 0;
    pool->free_capacity = SLAB_INITIAL_FREE_CAP;
    pool->free_list     = calloc(pool->free_capacity, sizeof(void *));
    if (!pool->free_list) {
        return GMX_ERR_ALLOC;
    }
    return GMX_OK;
}

void gmx_slab_pool_destroy(gmx_slab_pool_t *pool)
{
    if (!pool) return;
    /* Free any objects remaining in the free list */
    for (size_t i = 0; i < pool->free_count; i++) {
        free(pool->free_list[i]);
    }
    free(pool->free_list);
    pool->free_list = NULL;
    pool->free_count = 0;
}

void *gmx_slab_alloc(gmx_slab_pool_t *pool)
{
    if (!pool) return NULL;

    void *obj = NULL;
    if (pool->free_count > 0) {
        pool->free_count--;
        obj = pool->free_list[pool->free_count];
        memset(obj, 0, pool->object_size);
    } else {
        obj = calloc(1, pool->object_size);
    }
    if (obj) {
        pool->total_allocs++;
        pool->generation++;
    }
    return obj;
}

void gmx_slab_free(gmx_slab_pool_t *pool, void *ptr)
{
    if (!pool || !ptr) return;

    pool->total_frees++;

    /* Try to add to free list for reuse */
    if (pool->free_count < pool->free_capacity) {
        pool->free_list[pool->free_count++] = ptr;
    } else {
        /* Grow free list */
        size_t new_cap = pool->free_capacity * 2;
        void **new_list = realloc(pool->free_list, new_cap * sizeof(void *));
        if (new_list) {
            pool->free_list = new_list;
            pool->free_capacity = new_cap;
            pool->free_list[pool->free_count++] = ptr;
        } else {
            free(ptr);
        }
    }
}

uint32_t gmx_slab_generation(const gmx_slab_pool_t *pool)
{
    return pool ? pool->generation : 0;
}
