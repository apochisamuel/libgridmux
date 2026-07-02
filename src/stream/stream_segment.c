/*
 * libgridmux - Stream segment management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Stream data is divided into fixed-size segments for ordered
 * delivery and efficient compaction. Each segment has a monotonic
 * generation number and epoch assignment.
 */
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_STREAM_SEGMENT_SIZE  1024
#define GMX_STREAM_SEGMENT_MAX   64

typedef struct gmx_stream_segment {
    uint32_t  segment_id;
    uint32_t  stream_id;
    uint32_t  epoch;
    uint32_t  generation;
    uint8_t  *data;
    size_t    data_len;
    size_t    data_cap;
    bool      sealed;
    bool      active;
} gmx_stream_segment_t;

typedef struct gmx_stream_segment_table {
    gmx_stream_segment_t **segments;
    size_t                  capacity;
    size_t                  count;
    uint32_t                next_id;
    uint32_t                next_generation;
} gmx_stream_segment_table_t;

gmx_status_t gmx_stream_segment_table_init(gmx_stream_segment_table_t *st,
                                              size_t capacity)
{
    if (!st || capacity == 0) return GMX_ERR_BAD_STATE;
    st->segments = calloc(capacity, sizeof(gmx_stream_segment_t *));
    if (!st->segments) return GMX_ERR_ALLOC;
    st->capacity = capacity;
    st->count = 0;
    st->next_id = 1;
    st->next_generation = 1;
    return GMX_OK;
}

void gmx_stream_segment_table_destroy(gmx_stream_segment_table_t *st)
{
    if (!st || !st->segments) return;
    for (size_t i = 0; i < st->capacity; i++) {
        if (st->segments[i]) {
            free(st->segments[i]->data);
            free(st->segments[i]);
        }
    }
    free(st->segments);
    st->segments = NULL;
}

gmx_stream_segment_t *gmx_stream_segment_create(
    gmx_stream_segment_table_t *st,
    uint32_t stream_id,
    uint32_t epoch)
{
    if (!st || st->count >= st->capacity) return NULL;

    gmx_stream_segment_t *seg = calloc(1, sizeof(*seg));
    if (!seg) return NULL;

    seg->data = malloc(GMX_STREAM_SEGMENT_SIZE);
    if (!seg->data) { free(seg); return NULL; }

    seg->segment_id = st->next_id++;
    seg->stream_id  = stream_id;
    seg->epoch      = epoch;
    seg->generation = st->next_generation++;
    seg->data_len   = 0;
    seg->data_cap   = GMX_STREAM_SEGMENT_SIZE;
    seg->sealed     = false;
    seg->active     = true;

    for (size_t i = 0; i < st->capacity; i++) {
        if (!st->segments[i]) {
            st->segments[i] = seg;
            st->count++;
            return seg;
        }
    }

    free(seg->data);
    free(seg);
    return NULL;
}

gmx_stream_segment_t *gmx_stream_segment_lookup(
    gmx_stream_segment_table_t *st,
    uint32_t segment_id)
{
    if (!st) return NULL;
    for (size_t i = 0; i < st->capacity; i++) {
        gmx_stream_segment_t *s = st->segments[i];
        if (s && s->segment_id == segment_id && s->active) return s;
    }
    return NULL;
}

gmx_status_t gmx_stream_segment_write(gmx_stream_segment_t *seg,
                                         const uint8_t *data, size_t len)
{
    if (!seg || !seg->active || seg->sealed) return GMX_ERR_BAD_STATE;
    if (!data || len == 0) return GMX_ERR_BAD_LENGTH;

    size_t space = seg->data_cap - seg->data_len;
    size_t to_copy = len < space ? len : space;

    if (to_copy > 0) {
        memcpy(seg->data + seg->data_len, data, to_copy);
        seg->data_len += to_copy;
    }

    if (seg->data_len >= seg->data_cap) seg->sealed = true;
    return GMX_OK;
}

gmx_status_t gmx_stream_segment_seal(gmx_stream_segment_t *seg)
{
    if (!seg || !seg->active) return GMX_ERR_BAD_STATE;
    seg->sealed = true;
    return GMX_OK;
}

size_t gmx_stream_segment_remaining(const gmx_stream_segment_t *seg)
{
    if (!seg || seg->sealed) return 0;
    return seg->data_cap - seg->data_len;
}

uint32_t gmx_stream_segment_table_active_count(
    const gmx_stream_segment_table_t *st)
{
    if (!st) return 0;
    uint32_t count = 0;
    for (size_t i = 0; i < st->capacity; i++) {
        if (st->segments[i] && st->segments[i]->active) count++;
    }
    return count;
}

gmx_status_t gmx_stream_segment_retire(gmx_stream_segment_table_t *st,
                                          uint32_t segment_id)
{
    if (!st) return GMX_ERR_BAD_STATE;
    for (size_t i = 0; i < st->capacity; i++) {
        gmx_stream_segment_t *s = st->segments[i];
        if (s && s->segment_id == segment_id && s->active) {
            s->active = false;
            return GMX_OK;
        }
    }
    return GMX_ERR_NOT_FOUND;
}
