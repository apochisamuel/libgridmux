/*
 * libgridmux - Nonce management and anti-replay window
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Maintains a sliding window of recently seen nonces for each
 * channel to detect and reject replay attacks. The window tracks
 * the highest accepted nonce and a bitmap of accepted nonces
 * within the window range.
 */
#include "gmx/crypto.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_NONCE_WINDOW_SIZE  64
#define GMX_NONCE_BITMAP_WORDS (GMX_NONCE_WINDOW_SIZE / 32)

typedef struct gmx_nonce_window {
    uint64_t highest_nonce;
    uint32_t bitmap[GMX_NONCE_BITMAP_WORDS];
    uint32_t channel_id;
    uint32_t epoch;
    uint32_t accepted_count;
    uint32_t rejected_count;
    bool     initialized;
} gmx_nonce_window_t;

typedef struct gmx_nonce_manager {
    gmx_nonce_window_t *windows;
    size_t              capacity;
    size_t              active_count;
} gmx_nonce_manager_t;

gmx_status_t gmx_nonce_manager_init(gmx_nonce_manager_t *nm, size_t capacity)
{
    if (!nm || capacity == 0) return GMX_ERR_BAD_STATE;
    nm->windows = calloc(capacity, sizeof(gmx_nonce_window_t));
    if (!nm->windows) return GMX_ERR_ALLOC;
    nm->capacity = capacity;
    nm->active_count = 0;
    return GMX_OK;
}

void gmx_nonce_manager_destroy(gmx_nonce_manager_t *nm)
{
    if (!nm) return;
    free(nm->windows);
    nm->windows = NULL;
}

static gmx_nonce_window_t *find_or_create_window(gmx_nonce_manager_t *nm,
                                                    uint32_t channel_id)
{
    /* Find existing */
    for (size_t i = 0; i < nm->capacity; i++) {
        if (nm->windows[i].initialized &&
            nm->windows[i].channel_id == channel_id) {
            return &nm->windows[i];
        }
    }

    /* Create new */
    for (size_t i = 0; i < nm->capacity; i++) {
        if (!nm->windows[i].initialized) {
            gmx_nonce_window_t *w = &nm->windows[i];
            memset(w, 0, sizeof(*w));
            w->channel_id = channel_id;
            w->initialized = true;
            nm->active_count++;
            return w;
        }
    }
    return NULL;
}

static void set_bitmap_bit(uint32_t *bitmap, uint32_t bit_index)
{
    if (bit_index >= GMX_NONCE_WINDOW_SIZE) return;
    bitmap[bit_index / 32] |= (1u << (bit_index % 32));
}

static bool get_bitmap_bit(const uint32_t *bitmap, uint32_t bit_index)
{
    if (bit_index >= GMX_NONCE_WINDOW_SIZE) return false;
    return (bitmap[bit_index / 32] & (1u << (bit_index % 32))) != 0;
}

static void shift_bitmap_left(uint32_t *bitmap, uint32_t shift_amount)
{
    if (shift_amount >= GMX_NONCE_WINDOW_SIZE) {
        memset(bitmap, 0, GMX_NONCE_BITMAP_WORDS * sizeof(uint32_t));
        return;
    }

    uint32_t word_shift = shift_amount / 32;
    uint32_t bit_shift = shift_amount % 32;

    if (word_shift > 0) {
        for (uint32_t i = GMX_NONCE_BITMAP_WORDS; i > word_shift; i--) {
            bitmap[i - 1] = bitmap[i - 1 - word_shift];
        }
        for (uint32_t i = 0; i < word_shift; i++) {
            bitmap[i] = 0;
        }
    }

    if (bit_shift > 0) {
        for (uint32_t i = GMX_NONCE_BITMAP_WORDS; i > 1; i--) {
            bitmap[i - 1] = (bitmap[i - 1] << bit_shift) |
                            (bitmap[i - 2] >> (32 - bit_shift));
        }
        bitmap[0] <<= bit_shift;
    }
}

/*
 * Check and accept a nonce for the given channel.
 * Returns GMX_OK if the nonce is valid and has not been seen before.
 * Returns GMX_ERR_BAD_SEQUENCE if the nonce is a replay.
 */
gmx_status_t gmx_nonce_check(gmx_nonce_manager_t *nm,
                                uint32_t channel_id,
                                uint64_t nonce, uint32_t epoch)
{
    if (!nm) return GMX_ERR_BAD_STATE;

    gmx_nonce_window_t *w = find_or_create_window(nm, channel_id);
    if (!w) return GMX_ERR_OVERFLOW;

    w->epoch = epoch;

    /* First nonce for this channel */
    if (w->accepted_count == 0) {
        w->highest_nonce = nonce;
        set_bitmap_bit(w->bitmap, 0);
        w->accepted_count++;
        return GMX_OK;
    }

    /* Nonce is above current window → advance window */
    if (nonce > w->highest_nonce) {
        uint64_t diff = nonce - w->highest_nonce;
        if (diff < GMX_NONCE_WINDOW_SIZE) {
            shift_bitmap_left(w->bitmap, (uint32_t)diff);
        } else {
            memset(w->bitmap, 0, sizeof(w->bitmap));
        }
        w->highest_nonce = nonce;
        set_bitmap_bit(w->bitmap, 0);
        w->accepted_count++;
        return GMX_OK;
    }

    /* Nonce is within the current window → check bitmap */
    uint64_t diff = w->highest_nonce - nonce;
    if (diff >= GMX_NONCE_WINDOW_SIZE) {
        /* Too old, outside window */
        w->rejected_count++;
        return GMX_ERR_BAD_SEQUENCE;
    }

    /* Check if already seen */
    if (get_bitmap_bit(w->bitmap, (uint32_t)diff)) {
        w->rejected_count++;
        return GMX_ERR_BAD_SEQUENCE;
    }

    /* Accept and mark */
    set_bitmap_bit(w->bitmap, (uint32_t)diff);
    w->accepted_count++;
    return GMX_OK;
}

uint32_t gmx_nonce_accepted_count(const gmx_nonce_manager_t *nm,
                                    uint32_t channel_id)
{
    if (!nm) return 0;
    for (size_t i = 0; i < nm->capacity; i++) {
        if (nm->windows[i].initialized &&
            nm->windows[i].channel_id == channel_id) {
            return nm->windows[i].accepted_count;
        }
    }
    return 0;
}

uint32_t gmx_nonce_rejected_count(const gmx_nonce_manager_t *nm,
                                    uint32_t channel_id)
{
    if (!nm) return 0;
    for (size_t i = 0; i < nm->capacity; i++) {
        if (nm->windows[i].initialized &&
            nm->windows[i].channel_id == channel_id) {
            return nm->windows[i].rejected_count;
        }
    }
    return 0;
}

gmx_status_t gmx_nonce_reset_channel(gmx_nonce_manager_t *nm,
                                        uint32_t channel_id)
{
    if (!nm) return GMX_ERR_BAD_STATE;
    for (size_t i = 0; i < nm->capacity; i++) {
        if (nm->windows[i].initialized &&
            nm->windows[i].channel_id == channel_id) {
            memset(&nm->windows[i], 0, sizeof(nm->windows[i]));
            nm->active_count--;
            return GMX_OK;
        }
    }
    return GMX_ERR_NOT_FOUND;
}

size_t gmx_nonce_active_channels(const gmx_nonce_manager_t *nm)
{
    return nm ? nm->active_count : 0;
}
