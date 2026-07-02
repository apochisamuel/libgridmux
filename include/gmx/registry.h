/*
 * libgridmux - Central object registry with lifecycle tracking
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_REGISTRY_H
#define GMX_REGISTRY_H

#include "types.h"
#include "error.h"

#define GMX_REG_TYPE_CHANNEL  1
#define GMX_REG_TYPE_STREAM   2
#define GMX_REG_TYPE_ROUTE    3
#define GMX_REG_TYPE_LEASE    4
#define GMX_REG_TYPE_SEGMENT  5

typedef struct gmx_registry_entry {
    uint32_t  id;
    uint32_t  type;
    void     *object;
    uint32_t  generation;
    uint32_t  epoch_created;
    bool      active;
    struct gmx_registry_entry *next;
} gmx_registry_entry_t;

struct gmx_registry {
    gmx_registry_entry_t **buckets;
    size_t    bucket_count;
    size_t    entry_count;
    uint32_t  current_epoch;
    uint32_t  next_generation;
};

gmx_status_t  gmx_registry_init(gmx_registry_t *reg, size_t bucket_count);
void          gmx_registry_destroy(gmx_registry_t *reg);
gmx_status_t  gmx_registry_insert(gmx_registry_t *reg, uint32_t id,
                                    uint32_t type, void *object,
                                    uint32_t epoch);
void         *gmx_registry_lookup(gmx_registry_t *reg, uint32_t id,
                                   uint32_t type);
void         *gmx_registry_lookup_entry(gmx_registry_t *reg, uint32_t id,
                                          uint32_t type);
gmx_status_t  gmx_registry_remove(gmx_registry_t *reg, uint32_t id,
                                    uint32_t type);
void          gmx_registry_set_epoch(gmx_registry_t *reg, uint32_t epoch);
size_t        gmx_registry_gc_stale(gmx_registry_t *reg,
                                     uint32_t max_epoch, uint32_t type);

#endif /* GMX_REGISTRY_H */
