/*
 * libgridmux - Per-tenant quota management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/quota.h"
#include <stdlib.h>
#include <string.h>

gmx_status_t gmx_quota_table_init(gmx_quota_table_t *qt, size_t capacity)
{
    if (!qt || capacity == 0) return GMX_ERR_BAD_STATE;
    qt->buckets = calloc(capacity, sizeof(gmx_quota_bucket_t));
    if (!qt->buckets) return GMX_ERR_ALLOC;
    qt->capacity = capacity;
    qt->count = 0;
    qt->next_generation = 1;
    return GMX_OK;
}

void gmx_quota_table_destroy(gmx_quota_table_t *qt)
{
    if (!qt) return;
    free(qt->buckets);
    qt->buckets = NULL;
    qt->count = 0;
}

gmx_quota_bucket_t *gmx_quota_create_bucket(gmx_quota_table_t *qt,
                                              uint32_t group_id,
                                              uint32_t limit,
                                              uint32_t epoch)
{
    if (!qt || qt->count >= qt->capacity) return NULL;

    /* Check for duplicate */
    for (size_t i = 0; i < qt->capacity; i++) {
        if (qt->buckets[i].active && qt->buckets[i].group_id == group_id) {
            return NULL;
        }
    }

    /* Find free slot */
    for (size_t i = 0; i < qt->capacity; i++) {
        if (!qt->buckets[i].active) {
            gmx_quota_bucket_t *b = &qt->buckets[i];
            b->group_id      = group_id;
            b->allocated     = 0;
            b->consumed      = 0;
            b->limit         = limit;
            b->active_count  = 0;
            b->epoch_created = epoch;
            b->generation    = qt->next_generation++;
            b->active        = true;
            qt->count++;
            return b;
        }
    }
    return NULL;
}

gmx_quota_bucket_t *gmx_quota_lookup_bucket(gmx_quota_table_t *qt,
                                              uint32_t group_id)
{
    if (!qt) return NULL;
    for (size_t i = 0; i < qt->capacity; i++) {
        if (qt->buckets[i].active && qt->buckets[i].group_id == group_id) {
            return &qt->buckets[i];
        }
    }
    return NULL;
}

gmx_status_t gmx_quota_destroy_bucket(gmx_quota_table_t *qt,
                                        uint32_t group_id)
{
    if (!qt) return GMX_ERR_BAD_STATE;
    for (size_t i = 0; i < qt->capacity; i++) {
        if (qt->buckets[i].active && qt->buckets[i].group_id == group_id) {
            qt->buckets[i].active = false;
            qt->count--;
            return GMX_OK;
        }
    }
    return GMX_ERR_NOT_FOUND;
}

gmx_status_t gmx_quota_consume(gmx_quota_bucket_t *bucket, uint32_t amount)
{
    if (!bucket || !bucket->active) return GMX_ERR_NOT_ACTIVE;
    if (bucket->consumed + amount > bucket->limit) return GMX_ERR_OVERFLOW;
    bucket->consumed += amount;
    return GMX_OK;
}

gmx_status_t gmx_quota_release(gmx_quota_bucket_t *bucket, uint32_t amount)
{
    if (!bucket || !bucket->active) return GMX_ERR_NOT_ACTIVE;
    if (amount > bucket->consumed) return GMX_ERR_OVERFLOW;
    bucket->consumed -= amount;
    return GMX_OK;
}

gmx_status_t gmx_quota_join_group(gmx_quota_bucket_t *bucket)
{
    if (!bucket || !bucket->active) return GMX_ERR_NOT_ACTIVE;
    bucket->active_count++;
    return GMX_OK;
}

gmx_status_t gmx_quota_leave_group(gmx_quota_bucket_t *bucket)
{
    if (!bucket || !bucket->active) return GMX_ERR_NOT_ACTIVE;
    if (bucket->active_count == 0) return GMX_ERR_OVERFLOW;
    bucket->active_count--;
    return GMX_OK;
}

bool gmx_quota_gc_eligible(const gmx_quota_bucket_t *bucket)
{
    if (!bucket || !bucket->active) return false;
    return bucket->active_count == 0;
}
