/*
 * libgridmux - Lease-based reference management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_LEASE_H
#define GMX_LEASE_H

#include "types.h"
#include "error.h"

#define GMX_LEASE_MAX_HISTORY   16
#define GMX_LEASE_REFCOUNT_MAX  16

struct gmx_lease {
    uint32_t  lease_id;
    uint32_t  stream_id;
    uint32_t  epoch_acquired;
    uint32_t  generation;
    uint32_t  refcount;
    uint32_t  history_depth;
    void     *segment_ref;     /* cached reference to segment storage */
    size_t    segment_size;
    bool      active;
    bool      extended;
};

struct gmx_lease_table {
    gmx_lease_t **entries;
    size_t        capacity;
    size_t        count;
    uint32_t      next_generation;
};

gmx_status_t  gmx_lease_table_init(gmx_lease_table_t *lt, size_t capacity);
void          gmx_lease_table_destroy(gmx_lease_table_t *lt);

gmx_lease_t  *gmx_lease_acquire(gmx_lease_table_t *lt, uint32_t lease_id,
                                  uint32_t stream_id, uint32_t epoch,
                                  void *segment, size_t seg_size);
gmx_status_t  gmx_lease_release(gmx_lease_table_t *lt, uint32_t lease_id);
gmx_lease_t  *gmx_lease_lookup(gmx_lease_table_t *lt, uint32_t lease_id);

gmx_status_t  gmx_lease_extend_history(gmx_lease_t *lease, uint32_t amount);
gmx_status_t  gmx_lease_add_ref(gmx_lease_t *lease);
gmx_status_t  gmx_lease_dec_ref(gmx_lease_t *lease);

#endif /* GMX_LEASE_H */
