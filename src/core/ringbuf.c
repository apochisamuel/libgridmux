/*
 * libgridmux - Generic ring buffer implementation
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * A generic fixed-capacity ring buffer used for frame queuing,
 * event buffering, and I/O staging. Supports peek, skip, batch
 * read/write, and watermark-based flow control.
 */
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

typedef struct gmx_ringbuf {
    uint8_t  *data;
    size_t    capacity;
    size_t    head;       /* next read position */
    size_t    tail;       /* next write position */
    size_t    count;      /* bytes currently buffered */
    size_t    watermark;  /* high-water mark for flow control */
    uint64_t  total_written;
    uint64_t  total_read;
} gmx_ringbuf_t;

gmx_status_t gmx_ringbuf_init(gmx_ringbuf_t *rb, size_t capacity)
{
    if (!rb || capacity == 0) return GMX_ERR_BAD_STATE;
    rb->data = malloc(capacity);
    if (!rb->data) return GMX_ERR_ALLOC;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    rb->watermark = capacity * 3 / 4;
    rb->total_written = 0;
    rb->total_read = 0;
    return GMX_OK;
}

void gmx_ringbuf_destroy(gmx_ringbuf_t *rb)
{
    if (!rb) return;
    free(rb->data);
    rb->data = NULL;
    rb->capacity = 0;
}

size_t gmx_ringbuf_write(gmx_ringbuf_t *rb, const uint8_t *src, size_t len)
{
    if (!rb || !src || len == 0) return 0;
    size_t space = rb->capacity - rb->count;
    if (len > space) len = space;
    if (len == 0) return 0;

    /* Write in up to two segments (wrap-around) */
    size_t first = rb->capacity - rb->tail;
    if (first > len) first = len;
    memcpy(rb->data + rb->tail, src, first);

    size_t second = len - first;
    if (second > 0) {
        memcpy(rb->data, src + first, second);
    }

    rb->tail = (rb->tail + len) % rb->capacity;
    rb->count += len;
    rb->total_written += len;
    return len;
}

size_t gmx_ringbuf_read(gmx_ringbuf_t *rb, uint8_t *dst, size_t len)
{
    if (!rb || !dst || len == 0) return 0;
    if (len > rb->count) len = rb->count;
    if (len == 0) return 0;

    size_t first = rb->capacity - rb->head;
    if (first > len) first = len;
    memcpy(dst, rb->data + rb->head, first);

    size_t second = len - first;
    if (second > 0) {
        memcpy(dst + first, rb->data, second);
    }

    rb->head = (rb->head + len) % rb->capacity;
    rb->count -= len;
    rb->total_read += len;
    return len;
}

size_t gmx_ringbuf_peek(const gmx_ringbuf_t *rb, uint8_t *dst, size_t len)
{
    if (!rb || !dst || len == 0 || rb->count == 0) return 0;
    if (len > rb->count) len = rb->count;

    size_t first = rb->capacity - rb->head;
    if (first > len) first = len;
    memcpy(dst, rb->data + rb->head, first);

    size_t second = len - first;
    if (second > 0) {
        memcpy(dst + first, rb->data, second);
    }
    return len;
}

gmx_status_t gmx_ringbuf_skip(gmx_ringbuf_t *rb, size_t len)
{
    if (!rb) return GMX_ERR_BAD_STATE;
    if (len > rb->count) return GMX_ERR_OVERFLOW;
    rb->head = (rb->head + len) % rb->capacity;
    rb->count -= len;
    rb->total_read += len;
    return GMX_OK;
}

void gmx_ringbuf_clear(gmx_ringbuf_t *rb)
{
    if (!rb) return;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

size_t gmx_ringbuf_available(const gmx_ringbuf_t *rb)
{
    return rb ? rb->count : 0;
}

size_t gmx_ringbuf_space(const gmx_ringbuf_t *rb)
{
    return rb ? rb->capacity - rb->count : 0;
}

bool gmx_ringbuf_is_empty(const gmx_ringbuf_t *rb)
{
    return !rb || rb->count == 0;
}

bool gmx_ringbuf_is_full(const gmx_ringbuf_t *rb)
{
    return rb && rb->count >= rb->capacity;
}

bool gmx_ringbuf_above_watermark(const gmx_ringbuf_t *rb)
{
    return rb && rb->count >= rb->watermark;
}

gmx_status_t gmx_ringbuf_set_watermark(gmx_ringbuf_t *rb, size_t watermark)
{
    if (!rb || watermark > rb->capacity) return GMX_ERR_BAD_STATE;
    rb->watermark = watermark;
    return GMX_OK;
}

uint64_t gmx_ringbuf_total_written(const gmx_ringbuf_t *rb)
{
    return rb ? rb->total_written : 0;
}

uint64_t gmx_ringbuf_total_read(const gmx_ringbuf_t *rb)
{
    return rb ? rb->total_read : 0;
}
