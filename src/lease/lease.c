/*
 * libgridmux - Lease-based reference management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/lease.h"
#include <stdlib.h>
#include <string.h>

gmx_status_t gmx_lease_table_init(gmx_lease_table_t *lt, size_t capacity)
{
    if (!lt || capacity == 0) return GMX_ERR_BAD_STATE;
    lt->entries = calloc(capacity, sizeof(gmx_lease_t *));
    if (!lt->entries) return GMX_ERR_ALLOC;
    lt->capacity = capacity;
    lt->count = 0;
    lt->next_generation = 1;
    return GMX_OK;
}

void gmx_lease_table_destroy(gmx_lease_table_t *lt)
{
    if (!lt || !lt->entries) return;
    for (size_t i = 0; i < lt->capacity; i++) {
        free(lt->entries[i]);
    }
    free(lt->entries);
    lt->entries = NULL;
    lt->count = 0;
}

static size_t lease_slot(gmx_lease_table_t *lt, uint32_t lease_id)
{
    return (size_t)(lease_id % lt->capacity);
}

gmx_lease_t *gmx_lease_acquire(gmx_lease_table_t *lt, uint32_t lease_id,
                                uint32_t stream_id, uint32_t epoch,
                                void *segment, size_t seg_size)
{
    if (!lt || lt->count >= lt->capacity) return NULL;

    size_t slot = lease_slot(lt, lease_id);
    size_t orig = slot;
    while (lt->entries[slot] != NULL) {
        if (lt->entries[slot]->lease_id == lease_id &&
            lt->entries[slot]->active) {
            return NULL;
        }
        slot = (slot + 1) % lt->capacity;
        if (slot == orig) return NULL;
    }

    gmx_lease_t *lease = calloc(1, sizeof(*lease));
    if (!lease) return NULL;

    lease->lease_id       = lease_id;
    lease->stream_id      = stream_id;
    lease->epoch_acquired = epoch;
    lease->generation     = lt->next_generation++;
    lease->refcount       = 1;
    lease->history_depth  = 0;
    lease->segment_ref    = segment;
    lease->segment_size   = seg_size;
    lease->active         = true;
    lease->extended       = false;

    lt->entries[slot] = lease;
    lt->count++;
    return lease;
}

gmx_status_t gmx_lease_release(gmx_lease_table_t *lt, uint32_t lease_id)
{
    if (!lt) return GMX_ERR_BAD_STATE;
    size_t slot = lease_slot(lt, lease_id);
    size_t orig = slot;
    do {
        gmx_lease_t *l = lt->entries[slot];
        if (!l) return GMX_ERR_NOT_FOUND;
        if (l->lease_id == lease_id && l->active) {
            l->active = false;
            l->refcount = 0;
            return GMX_OK;
        }
        slot = (slot + 1) % lt->capacity;
    } while (slot != orig);
    return GMX_ERR_NOT_FOUND;
}

gmx_lease_t *gmx_lease_lookup(gmx_lease_table_t *lt, uint32_t lease_id)
{
    if (!lt) return NULL;
    size_t slot = lease_slot(lt, lease_id);
    size_t orig = slot;
    do {
        gmx_lease_t *l = lt->entries[slot];
        if (!l) return NULL;
        if (l->lease_id == lease_id && l->active) return l;
        slot = (slot + 1) % lt->capacity;
    } while (slot != orig);
    return NULL;
}

gmx_status_t gmx_lease_extend_history(gmx_lease_t *lease, uint32_t amount)
{
    if (!lease || !lease->active) return GMX_ERR_BAD_STATE;
    if (amount == 0) return GMX_ERR_BAD_LENGTH;
    lease->history_depth += amount;
    lease->extended = true;
    return GMX_OK;
}

gmx_status_t gmx_lease_add_ref(gmx_lease_t *lease)
{
    if (!lease || !lease->active) return GMX_ERR_BAD_STATE;
    if (lease->refcount >= GMX_LEASE_REFCOUNT_MAX) return GMX_ERR_OVERFLOW;
    lease->refcount++;
    return GMX_OK;
}

gmx_status_t gmx_lease_dec_ref(gmx_lease_t *lease)
{
    if (!lease) return GMX_ERR_BAD_STATE;
    if (lease->refcount == 0) return GMX_ERR_OVERFLOW;
    lease->refcount--;
    return GMX_OK;
}
