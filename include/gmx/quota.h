/*
 * libgridmux - Per-tenant quota management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_QUOTA_H
#define GMX_QUOTA_H

#include "types.h"
#include "error.h"

#define GMX_QUOTA_MAX_BUCKETS   32
#define GMX_QUOTA_MAX_GROUP     16

typedef struct gmx_quota_bucket {
    uint32_t  group_id;
    uint32_t  allocated;
    uint32_t  consumed;
    uint32_t  limit;
    uint32_t  active_count;       /* channels referencing this bucket */
    uint32_t  epoch_created;
    uint32_t  generation;
    bool      active;
} gmx_quota_bucket_t;

typedef struct gmx_quota_table {
    gmx_quota_bucket_t *buckets;
    size_t              capacity;
    size_t              count;
    uint32_t            next_generation;
} gmx_quota_table_t;

gmx_status_t          gmx_quota_table_init(gmx_quota_table_t *qt, size_t capacity);
void                  gmx_quota_table_destroy(gmx_quota_table_t *qt);

gmx_quota_bucket_t   *gmx_quota_create_bucket(gmx_quota_table_t *qt,
                                                uint32_t group_id,
                                                uint32_t limit,
                                                uint32_t epoch);
gmx_quota_bucket_t   *gmx_quota_lookup_bucket(gmx_quota_table_t *qt,
                                                uint32_t group_id);
gmx_status_t          gmx_quota_destroy_bucket(gmx_quota_table_t *qt,
                                                uint32_t group_id);

gmx_status_t          gmx_quota_consume(gmx_quota_bucket_t *bucket, uint32_t amount);
gmx_status_t          gmx_quota_release(gmx_quota_bucket_t *bucket, uint32_t amount);
gmx_status_t          gmx_quota_join_group(gmx_quota_bucket_t *bucket);
gmx_status_t          gmx_quota_leave_group(gmx_quota_bucket_t *bucket);

/* Check if bucket is eligible for GC (active_count == 0) */
bool                  gmx_quota_gc_eligible(const gmx_quota_bucket_t *bucket);

#endif /* GMX_QUOTA_H */
