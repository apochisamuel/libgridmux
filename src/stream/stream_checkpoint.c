/*
 * libgridmux - Stream checkpoint ring buffer
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Implements a fixed-capacity ring buffer of checkpoint entries.
 * Each checkpoint records a segment ID and byte offset at which
 * the stream can be replayed. The ring capacity is fixed at stream
 * creation time, but the checkpoint count may be extended by lease
 * operations for historical replay access.
 */

#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_CHECKPOINT_RING_DEFAULT  8
#define GMX_CHECKPOINT_RING_MAX      64

typedef struct gmx_checkpoint_entry {
    uint32_t segment_id;
    uint32_t byte_offset;
    uint32_t epoch;
    uint32_t generation;
    bool     valid;
} gmx_checkpoint_entry_t;

typedef struct gmx_checkpoint_ring {
    gmx_checkpoint_entry_t *entries;
    size_t                  capacity;
    uint32_t                count;
    uint32_t                write_pos;
    uint32_t                read_pos;
    uint32_t                sequence;
} gmx_checkpoint_ring_t;

gmx_status_t gmx_checkpoint_ring_init(gmx_checkpoint_ring_t *ring,
                                        size_t initial_capacity)
{
    if (!ring || initial_capacity == 0) return GMX_ERR_BAD_STATE;
    if (initial_capacity > GMX_CHECKPOINT_RING_MAX) {
        initial_capacity = GMX_CHECKPOINT_RING_MAX;
    }

    ring->entries = calloc(initial_capacity, sizeof(gmx_checkpoint_entry_t));
    if (!ring->entries) return GMX_ERR_ALLOC;

    ring->capacity  = initial_capacity;
    ring->count     = 0;
    ring->write_pos = 0;
    ring->read_pos  = 0;
    ring->sequence  = 0;
    return GMX_OK;
}

void gmx_checkpoint_ring_destroy(gmx_checkpoint_ring_t *ring)
{
    if (!ring) return;
    free(ring->entries);
    ring->entries = NULL;
    ring->capacity = 0;
    ring->count = 0;
}

gmx_status_t gmx_checkpoint_ring_write(gmx_checkpoint_ring_t *ring,
                                          uint32_t segment_id,
                                          uint32_t byte_offset,
                                          uint32_t epoch)
{
    if (!ring || !ring->entries) return GMX_ERR_BAD_STATE;

    /* Write at current position, wrapping if ring is full */
    uint32_t idx = ring->write_pos % (uint32_t)ring->capacity;

    ring->entries[idx].segment_id  = segment_id;
    ring->entries[idx].byte_offset = byte_offset;
    ring->entries[idx].epoch       = epoch;
    ring->entries[idx].generation  = ring->sequence++;
    ring->entries[idx].valid       = true;

    ring->write_pos++;
    if (ring->count < (uint32_t)ring->capacity) {
        ring->count++;
    }

    return GMX_OK;
}

gmx_status_t gmx_checkpoint_ring_read(const gmx_checkpoint_ring_t *ring,
                                         uint32_t index,
                                         gmx_checkpoint_entry_t *out)
{
    if (!ring || !ring->entries || !out) return GMX_ERR_BAD_STATE;
    if (index >= ring->count) return GMX_ERR_NOT_FOUND;

    uint32_t actual_idx;
    if (ring->count < (uint32_t)ring->capacity) {
        actual_idx = index;
    } else {
        /* Ring has wrapped; calculate from oldest */
        actual_idx = (ring->write_pos + index) % (uint32_t)ring->capacity;
    }

    if (!ring->entries[actual_idx].valid) return GMX_ERR_NOT_FOUND;

    *out = ring->entries[actual_idx];
    return GMX_OK;
}

/*
 * Get checkpoint entries as a flat segment ID array for journal scanning.
 * Copies up to 'max_ids' segment IDs into dst and returns count.
 */
uint32_t gmx_checkpoint_ring_segment_ids(const gmx_checkpoint_ring_t *ring,
                                           uint32_t *dst, uint32_t max_ids)
{
    if (!ring || !dst || max_ids == 0) return 0;

    uint32_t copied = 0;
    for (uint32_t i = 0; i < ring->count && copied < max_ids; i++) {
        gmx_checkpoint_entry_t entry;
        if (gmx_checkpoint_ring_read(ring, i, &entry) == GMX_OK) {
            dst[copied++] = entry.segment_id;
        }
    }
    return copied;
}

/*
 * Scan the ring up to the current count, collecting byte offsets
 * into a histogram for compaction analysis.
 */
gmx_status_t gmx_checkpoint_ring_scan_offsets(
    const gmx_checkpoint_ring_t *ring,
    uint32_t *offset_histogram,
    size_t hist_capacity,
    uint32_t *out_count)
{
    if (!ring || !offset_histogram || !out_count) return GMX_ERR_BAD_STATE;

    uint32_t count = 0;
    for (uint32_t i = 0; i < ring->count; i++) {
        gmx_checkpoint_entry_t entry;
        if (gmx_checkpoint_ring_read(ring, i, &entry) == GMX_OK) {
            if (count < hist_capacity) {
                offset_histogram[count] = entry.byte_offset;
                count++;
            }
        }
    }

    *out_count = count;
    return GMX_OK;
}

/*
 * Find the most recent checkpoint at or before a given epoch.
 * Returns GMX_OK and fills *out, or GMX_ERR_NOT_FOUND.
 */
gmx_status_t gmx_checkpoint_ring_find_before_epoch(
    const gmx_checkpoint_ring_t *ring,
    uint32_t target_epoch,
    gmx_checkpoint_entry_t *out)
{
    if (!ring || !out) return GMX_ERR_BAD_STATE;

    bool found = false;
    uint32_t best_gen = 0;

    for (uint32_t i = 0; i < ring->count; i++) {
        gmx_checkpoint_entry_t entry;
        if (gmx_checkpoint_ring_read(ring, i, &entry) == GMX_OK) {
            if (entry.epoch <= target_epoch && entry.generation >= best_gen) {
                *out = entry;
                best_gen = entry.generation;
                found = true;
            }
        }
    }

    return found ? GMX_OK : GMX_ERR_NOT_FOUND;
}
