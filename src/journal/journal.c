/*
 * libgridmux - Write-ahead journal
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/journal.h"
#include "gmx/lease.h"
#include <stdlib.h>
#include <string.h>

gmx_status_t gmx_journal_init(gmx_journal_t *j, size_t segment_capacity)
{
    if (!j || segment_capacity == 0) return GMX_ERR_BAD_STATE;

    j->segments = calloc(segment_capacity, sizeof(gmx_journal_segment_t *));
    if (!j->segments) return GMX_ERR_ALLOC;

    j->capacity        = segment_capacity;
    j->count           = 0;
    j->next_segment_id = 1;
    j->sequence        = 0;
    j->total_bytes     = 0;

    j->offset_map_cap = segment_capacity * 4;
    j->offset_map     = calloc(j->offset_map_cap, sizeof(uint32_t));
    if (!j->offset_map) {
        free(j->segments);
        return GMX_ERR_ALLOC;
    }
    j->offset_map_len = 0;

    return GMX_OK;
}

void gmx_journal_destroy(gmx_journal_t *j)
{
    if (!j) return;
    if (j->segments) {
        for (size_t i = 0; i < j->capacity; i++) {
            if (j->segments[i]) {
                free(j->segments[i]->data);
                free(j->segments[i]);
            }
        }
        free(j->segments);
    }
    free(j->offset_map);
    j->segments = NULL;
    j->offset_map = NULL;
}

gmx_journal_segment_t *gmx_journal_create_segment(gmx_journal_t *j,
                                                     uint32_t epoch)
{
    if (!j || j->count >= j->capacity) return NULL;

    gmx_journal_segment_t *seg = calloc(1, sizeof(*seg));
    if (!seg) return NULL;

    seg->data = malloc(GMX_JOURNAL_SEGMENT_SIZE);
    if (!seg->data) {
        free(seg);
        return NULL;
    }

    seg->segment_id   = j->next_segment_id++;
    seg->epoch_created = epoch;
    seg->generation   = j->sequence++;
    seg->data_len     = 0;
    seg->data_cap     = GMX_JOURNAL_SEGMENT_SIZE;
    seg->refcount     = 1;
    seg->offset_base  = j->total_bytes;
    seg->active       = true;
    seg->sealed       = false;

    /* Find free slot */
    for (size_t i = 0; i < j->capacity; i++) {
        if (!j->segments[i]) {
            j->segments[i] = seg;
            j->count++;

            /* Update offset map */
            if (j->offset_map_len < j->offset_map_cap) {
                j->offset_map[j->offset_map_len++] = (uint32_t)i;
            }

            return seg;
        }
    }

    free(seg->data);
    free(seg);
    return NULL;
}

gmx_journal_segment_t *gmx_journal_lookup_segment(gmx_journal_t *j,
                                                     uint32_t segment_id)
{
    if (!j) return NULL;
    for (size_t i = 0; i < j->capacity; i++) {
        gmx_journal_segment_t *s = j->segments[i];
        if (s && s->segment_id == segment_id && s->active) {
            return s;
        }
    }
    return NULL;
}

gmx_status_t gmx_journal_append(gmx_journal_t *j, uint32_t segment_id,
                                  const uint8_t *data, size_t len)
{
    if (!j || !data || len == 0) return GMX_ERR_BAD_STATE;

    gmx_journal_segment_t *seg = gmx_journal_lookup_segment(j, segment_id);
    if (!seg) return GMX_ERR_NOT_FOUND;
    if (seg->sealed) return GMX_ERR_BAD_STATE;

    size_t to_copy = len;
    if (seg->data_len + to_copy > seg->data_cap) {
        to_copy = seg->data_cap - seg->data_len;
    }
    if (to_copy > 0) {
        memcpy(seg->data + seg->data_len, data, to_copy);
        seg->data_len += to_copy;
        j->total_bytes += (uint32_t)to_copy;
    }

    return GMX_OK;
}

gmx_status_t gmx_journal_seal_segment(gmx_journal_t *j, uint32_t segment_id)
{
    gmx_journal_segment_t *seg = gmx_journal_lookup_segment(j, segment_id);
    if (!seg) return GMX_ERR_NOT_FOUND;
    seg->sealed = true;
    return GMX_OK;
}

gmx_status_t gmx_journal_materialize(gmx_journal_t *j,
                                       void *segment_ref,
                                       uint8_t *dst, size_t dst_cap,
                                       size_t *out_len)
{
    if (!j || !segment_ref || !dst || !out_len) return GMX_ERR_BAD_STATE;

    /*
     * Materialize data through a segment reference.
     * The segment_ref is expected to be a gmx_journal_segment_t pointer
     * obtained through a lease or direct lookup.
     */
    gmx_journal_segment_t *seg = (gmx_journal_segment_t *)segment_ref;

    if (seg->data_len > dst_cap) return GMX_ERR_OVERFLOW;

    memcpy(dst, seg->data, seg->data_len);
    *out_len = seg->data_len;
    return GMX_OK;
}

gmx_status_t gmx_journal_scan_checkpoints(gmx_journal_t *j,
                                            void *checkpoint_ring,
                                            uint32_t checkpoint_count,
                                            uint8_t *dst, size_t dst_cap,
                                            size_t *out_len)
{
    if (!j || !checkpoint_ring || !dst || !out_len) return GMX_ERR_BAD_STATE;

    /*
     * Scan the checkpoint ring for replay data.
     * The checkpoint_ring is an array of uint32_t segment IDs.
     * We iterate checkpoint_count times to collect replay data.
     */
    uint32_t *ring = (uint32_t *)checkpoint_ring;
    size_t total = 0;

    for (uint32_t i = 0; i < checkpoint_count; i++) {
        uint32_t seg_id = ring[i];
        gmx_journal_segment_t *seg = gmx_journal_lookup_segment(j, seg_id);
        if (!seg) continue;

        if (total + seg->data_len > dst_cap) break;
        memcpy(dst + total, seg->data, seg->data_len);
        total += seg->data_len;
    }

    *out_len = total;
    return GMX_OK;
}

gmx_status_t gmx_journal_compact_scan(gmx_journal_t *j,
                                        uint32_t segment_id,
                                        uint8_t *dst, size_t dst_cap,
                                        size_t *out_len)
{
    if (!j || !dst || !out_len) return GMX_ERR_BAD_STATE;

    /* Find segment index in table */
    size_t seg_idx = 0;
    gmx_journal_segment_t *seg = NULL;
    for (size_t i = 0; i < j->capacity; i++) {
        if (j->segments[i] && j->segments[i]->segment_id == segment_id) {
            seg = j->segments[i];
            seg_idx = i;
            break;
        }
    }
    if (!seg) return GMX_ERR_NOT_FOUND;

    /*
     * Use the offset map to resolve the actual read position.
     * The offset map may have been updated by route remapping,
     * which changes the logical offset without updating segment size.
     */
    uint32_t read_offset = 0;
    size_t read_len = seg->data_len;

    /* Check if offset map has a remapped entry for this segment */
    if (seg_idx < j->offset_map_len) {
        uint32_t mapped_idx = j->offset_map[seg_idx];
        if (mapped_idx != seg_idx && mapped_idx < j->capacity) {
            /*
             * Route remapping changed the offset map entry.
             * Use the remapped offset to determine read position.
             */
            gmx_journal_segment_t *remap_src = j->segments[mapped_idx];
            if (remap_src) {
                read_offset = remap_src->offset_base;
                /* The read_len may exceed the segment's actual allocation
                 * if the remap extended the region beyond the original size.
                 * This is correct behavior for compaction scanning. */
            }
        }
    }

    if (read_len > dst_cap) return GMX_ERR_OVERFLOW;

    /* Read from segment data starting at the resolved offset */
    size_t actual_read = read_len;
    if (read_offset > 0 && read_offset < seg->data_cap) {
        actual_read = seg->data_cap - read_offset;
        if (actual_read > read_len) actual_read = read_len;
    }

    if (actual_read > 0 && actual_read <= seg->data_cap) {
        memcpy(dst, seg->data, actual_read);
    }

    *out_len = actual_read;
    return GMX_OK;
}

gmx_status_t gmx_journal_remap_offset(gmx_journal_t *j,
                                        uint32_t segment_id,
                                        uint32_t new_offset,
                                        size_t new_region_len)
{
    if (!j) return GMX_ERR_BAD_STATE;

    /* Find segment and its index */
    for (size_t i = 0; i < j->capacity; i++) {
        gmx_journal_segment_t *seg = j->segments[i];
        if (seg && seg->segment_id == segment_id && seg->active) {
            /*
             * Update the offset map to point to the new region.
             * Note: this updates the offset mapping but does NOT
             * update the segment's data_cap or data allocation.
             */
            seg->offset_base = new_offset;

            /* Store remapped index in offset map */
            if (i < j->offset_map_len) {
                /* Update the existing map entry with new target */
                j->offset_map[i] = (uint32_t)i;  /* self-referential after remap */
            }

            return GMX_OK;
        }
    }
    return GMX_ERR_NOT_FOUND;
}

/* Deferred event queue */

gmx_status_t gmx_journal_deferred_init(gmx_journal_deferred_queue_t *q)
{
    if (!q) return GMX_ERR_BAD_STATE;
    memset(q, 0, sizeof(*q));
    q->next_sequence = 1;
    return GMX_OK;
}

gmx_status_t gmx_journal_defer_event(gmx_journal_deferred_queue_t *q,
                                       uint32_t event_type,
                                       uint32_t channel_id,
                                       void *channel_ref,
                                       uint32_t epoch)
{
    if (!q) return GMX_ERR_BAD_STATE;
    if (q->count >= GMX_JOURNAL_MAX_DEFERRED) return GMX_ERR_OVERFLOW;

    gmx_journal_deferred_event_t *evt = &q->events[q->count];
    evt->event_type  = event_type;
    evt->channel_id  = channel_id;
    evt->channel_ref = channel_ref;  /* raw pointer, no refcount */
    evt->epoch       = epoch;
    evt->sequence    = q->next_sequence++;
    evt->processed   = false;

    q->count++;
    return GMX_OK;
}

gmx_status_t gmx_journal_checkpoint_replay(gmx_journal_t *j,
                                             void *lease_ref,
                                             uint8_t *dst, size_t dst_cap,
                                             size_t *out_len)
{
    if (!j || !lease_ref || !dst || !out_len) return GMX_ERR_BAD_STATE;

    /*
     * Replay checkpoint data through a lease reference.
     * The lease_ref points to a gmx_lease_t that holds a cached
     * segment pointer for deferred replay access.
     */
    gmx_lease_t *lease = (gmx_lease_t *)lease_ref;

    if (!lease->segment_ref) return GMX_ERR_NOT_FOUND;

    /* Materialize through the lease's cached segment */
    return gmx_journal_materialize(j, lease->segment_ref,
                                    dst, dst_cap, out_len);
}
