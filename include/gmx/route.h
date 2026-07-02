/*
 * libgridmux - Grid-topology routing with route segments
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_ROUTE_H
#define GMX_ROUTE_H

#include "types.h"
#include "error.h"

struct gmx_route_entry {
    uint32_t  route_id;
    uint32_t  segment_id;
    uint32_t  generation;
    uint32_t  epoch_created;
    uint8_t  *segment_data;
    size_t    segment_len;
    uint32_t  subscriber_count;
    uint32_t  next_hop_addr;
    uint16_t  next_hop_port;
    uint8_t   metric;
    void     *registry_ref;     /* cached registry entry for fast metadata lookup */
    bool      active;
};

struct gmx_route_table {
    gmx_route_entry_t **entries;
    size_t              capacity;
    size_t              count;
    uint32_t            cascade_seq;
    uint32_t            table_generation;
};

gmx_status_t        gmx_route_table_init(gmx_route_table_t *table,
                                           size_t capacity);
void                gmx_route_table_destroy(gmx_route_table_t *table);

gmx_route_entry_t  *gmx_route_entry_create(gmx_route_table_t *table,
                                             uint32_t route_id,
                                             uint32_t segment_id,
                                             uint32_t epoch,
                                             const uint8_t *data,
                                             size_t data_len);

gmx_route_entry_t  *gmx_route_table_lookup(gmx_route_table_t *table,
                                             uint32_t route_id);

gmx_status_t        gmx_route_table_remove(gmx_route_table_t *table,
                                             uint32_t route_id);

/* Cascade update: replaces a route entry with new data */
gmx_status_t        gmx_route_cascade_update(gmx_route_table_t *table,
                                              uint32_t route_id,
                                              const uint8_t *new_data,
                                              size_t new_len);

#endif /* GMX_ROUTE_H */
