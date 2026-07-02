/*
 * libgridmux - Typed slab allocator with generation tracking
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_SLAB_H
#define GMX_SLAB_H

#include "types.h"
#include "error.h"

struct gmx_slab_pool {
    void   **free_list;
    size_t   free_count;
    size_t   free_capacity;
    size_t   object_size;
    uint32_t generation;
    uint32_t total_allocs;
    uint32_t total_frees;
};

gmx_status_t gmx_slab_pool_init(gmx_slab_pool_t *pool, size_t object_size);
void         gmx_slab_pool_destroy(gmx_slab_pool_t *pool);
void        *gmx_slab_alloc(gmx_slab_pool_t *pool);
void         gmx_slab_free(gmx_slab_pool_t *pool, void *ptr);
uint32_t     gmx_slab_generation(const gmx_slab_pool_t *pool);

#endif /* GMX_SLAB_H */
