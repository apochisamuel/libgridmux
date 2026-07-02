/*
 * libgridmux - Lease reference counting and ownership tracking
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Tracks reference ownership for leases across multiple consumers.
 * Each consumer (stream, journal, migration) acquires a reference
 * that prevents the lease from being reclaimed during compaction.
 */
#include "gmx/lease.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_LEASE_OWNER_MAX  8

typedef struct gmx_lease_owner {
    uint32_t owner_id;
    uint32_t owner_type;
    uint32_t epoch_acquired;
    bool     active;
} gmx_lease_owner_t;

typedef struct gmx_lease_refcount_tracker {
    gmx_lease_owner_t owners[GMX_LEASE_OWNER_MAX];
    uint32_t count;
    uint32_t total_refs;
    uint32_t total_derefs;
} gmx_lease_refcount_tracker_t;

gmx_status_t gmx_lease_refcount_init(gmx_lease_refcount_tracker_t *rt)
{
    if (!rt) return GMX_ERR_BAD_STATE;
    memset(rt, 0, sizeof(*rt));
    return GMX_OK;
}

gmx_status_t gmx_lease_refcount_acquire(gmx_lease_refcount_tracker_t *rt,
                                           uint32_t owner_id,
                                           uint32_t owner_type,
                                           uint32_t epoch)
{
    if (!rt || rt->count >= GMX_LEASE_OWNER_MAX) return GMX_ERR_OVERFLOW;

    /* Check for duplicate */
    for (uint32_t i = 0; i < GMX_LEASE_OWNER_MAX; i++) {
        if (rt->owners[i].active &&
            rt->owners[i].owner_id == owner_id &&
            rt->owners[i].owner_type == owner_type) {
            return GMX_ERR_DUPLICATE;
        }
    }

    /* Find free slot */
    for (uint32_t i = 0; i < GMX_LEASE_OWNER_MAX; i++) {
        if (!rt->owners[i].active) {
            rt->owners[i].owner_id = owner_id;
            rt->owners[i].owner_type = owner_type;
            rt->owners[i].epoch_acquired = epoch;
            rt->owners[i].active = true;
            rt->count++;
            rt->total_refs++;
            return GMX_OK;
        }
    }
    return GMX_ERR_OVERFLOW;
}

gmx_status_t gmx_lease_refcount_release(gmx_lease_refcount_tracker_t *rt,
                                            uint32_t owner_id,
                                            uint32_t owner_type)
{
    if (!rt) return GMX_ERR_BAD_STATE;

    for (uint32_t i = 0; i < GMX_LEASE_OWNER_MAX; i++) {
        if (rt->owners[i].active &&
            rt->owners[i].owner_id == owner_id &&
            rt->owners[i].owner_type == owner_type) {
            rt->owners[i].active = false;
            rt->count--;
            rt->total_derefs++;
            return GMX_OK;
        }
    }
    return GMX_ERR_NOT_FOUND;
}

bool gmx_lease_refcount_is_held(const gmx_lease_refcount_tracker_t *rt,
                                  uint32_t owner_id,
                                  uint32_t owner_type)
{
    if (!rt) return false;
    for (uint32_t i = 0; i < GMX_LEASE_OWNER_MAX; i++) {
        if (rt->owners[i].active &&
            rt->owners[i].owner_id == owner_id &&
            rt->owners[i].owner_type == owner_type) {
            return true;
        }
    }
    return false;
}

uint32_t gmx_lease_refcount_active(const gmx_lease_refcount_tracker_t *rt)
{
    return rt ? rt->count : 0;
}

bool gmx_lease_refcount_reclaimable(const gmx_lease_refcount_tracker_t *rt)
{
    return rt && rt->count == 0;
}

gmx_status_t gmx_lease_refcount_transfer(
    gmx_lease_refcount_tracker_t *src,
    gmx_lease_refcount_tracker_t *dst,
    uint32_t owner_id, uint32_t owner_type, uint32_t epoch)
{
    if (!src || !dst) return GMX_ERR_BAD_STATE;

    gmx_status_t rc = gmx_lease_refcount_release(src, owner_id, owner_type);
    if (rc != GMX_OK) return rc;

    return gmx_lease_refcount_acquire(dst, owner_id, owner_type, epoch);
}

size_t gmx_lease_refcount_collect_owners(
    const gmx_lease_refcount_tracker_t *rt,
    uint32_t *ids, uint32_t *types, size_t max)
{
    if (!rt || !ids || !types || max == 0) return 0;
    size_t count = 0;
    for (uint32_t i = 0; i < GMX_LEASE_OWNER_MAX && count < max; i++) {
        if (rt->owners[i].active) {
            ids[count] = rt->owners[i].owner_id;
            types[count] = rt->owners[i].owner_type;
            count++;
        }
    }
    return count;
}
