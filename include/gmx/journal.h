/*
 * libgridmux - Write-ahead journal for stream data
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_JOURNAL_H
#define GMX_JOURNAL_H

#include "types.h"
#include "error.h"

#define GMX_JOURNAL_MAX_SEGMENTS  64
#define GMX_JOURNAL_SEGMENT_SIZE  4096

typedef struct gmx_journal_segment {
    uint32_t  segment_id;
    uint32_t  epoch_created;
    uint32_t  generation;
    uint8_t  *data;
    size_t    data_len;
    size_t    data_cap;
    uint32_t  refcount;
    uint32_t  offset_base;   /* base offset in journal sequence */
    bool      active;
    bool      sealed;
} gmx_journal_segment_t;

typedef struct gmx_journal {
    gmx_journal_segment_t **segments;
    size_t                  capacity;
    size_t                  count;
    uint32_t                next_segment_id;
    uint32_t                sequence;
    uint32_t                total_bytes;
    uint32_t               *offset_map;      /* maps logical offset to segment index */
    size_t                  offset_map_cap;
    size_t                  offset_map_len;
} gmx_journal_t;

typedef struct gmx_journal_deferred_event {
    uint32_t  event_type;
    uint32_t  channel_id;
    void     *channel_ref;   /* raw pointer to originating channel */
    uint32_t  epoch;
    uint32_t  sequence;
    bool      processed;
} gmx_journal_deferred_event_t;

#define GMX_JOURNAL_MAX_DEFERRED  32

typedef struct gmx_journal_deferred_queue {
    gmx_journal_deferred_event_t events[GMX_JOURNAL_MAX_DEFERRED];
    size_t count;
    uint32_t next_sequence;
} gmx_journal_deferred_queue_t;

gmx_status_t  gmx_journal_init(gmx_journal_t *j, size_t segment_capacity);
void          gmx_journal_destroy(gmx_journal_t *j);

gmx_journal_segment_t *gmx_journal_create_segment(gmx_journal_t *j,
                                                     uint32_t epoch);
gmx_journal_segment_t *gmx_journal_lookup_segment(gmx_journal_t *j,
                                                     uint32_t segment_id);

gmx_status_t  gmx_journal_append(gmx_journal_t *j, uint32_t segment_id,
                                   const uint8_t *data, size_t len);
gmx_status_t  gmx_journal_seal_segment(gmx_journal_t *j, uint32_t segment_id);

/* Materialize data through a segment reference (e.g. via lease) */
gmx_status_t  gmx_journal_materialize(gmx_journal_t *j,
                                        void *segment_ref,
                                        uint8_t *dst, size_t dst_cap,
                                        size_t *out_len);

/* Scan checkpoint ring for replay */
gmx_status_t  gmx_journal_scan_checkpoints(gmx_journal_t *j,
                                             void *checkpoint_ring,
                                             uint32_t checkpoint_count,
                                             uint8_t *dst, size_t dst_cap,
                                             size_t *out_len);

/* Compact scan segment using offset map */
gmx_status_t  gmx_journal_compact_scan(gmx_journal_t *j,
                                         uint32_t segment_id,
                                         uint8_t *dst, size_t dst_cap,
                                         size_t *out_len);

/* Remap journal segment via route update */
gmx_status_t  gmx_journal_remap_offset(gmx_journal_t *j,
                                          uint32_t segment_id,
                                          uint32_t new_offset,
                                          size_t new_region_len);

/* Deferred event queue */
gmx_status_t  gmx_journal_deferred_init(gmx_journal_deferred_queue_t *q);
gmx_status_t  gmx_journal_defer_event(gmx_journal_deferred_queue_t *q,
                                        uint32_t event_type,
                                        uint32_t channel_id,
                                        void *channel_ref,
                                        uint32_t epoch);

/* Checkpoint replay through lease reference */
gmx_status_t  gmx_journal_checkpoint_replay(gmx_journal_t *j,
                                              void *lease_ref,
                                              uint8_t *dst, size_t dst_cap,
                                              size_t *out_len);

#endif /* GMX_JOURNAL_H */
