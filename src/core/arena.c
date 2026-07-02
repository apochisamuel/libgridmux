/*
 * libgridmux - Arena allocator for batch memory management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * The arena provides fast bump-pointer allocation for frame processing.
 * All memory is freed at once when the arena is reset, avoiding
 * per-object free overhead during batch frame ingestion.
 */

#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_ARENA_BLOCK_SIZE  (64 * 1024)
#define GMX_ARENA_MAX_BLOCKS  256
#define GMX_ARENA_ALIGN       8

typedef struct gmx_arena_block {
    uint8_t *data;
    size_t   size;
    size_t   used;
} gmx_arena_block_t;

typedef struct gmx_arena {
    gmx_arena_block_t blocks[GMX_ARENA_MAX_BLOCKS];
    size_t             block_count;
    size_t             current_block;
    size_t             total_allocated;
    size_t             total_used;
    size_t             peak_used;
} gmx_arena_t;

static size_t arena_align_up(size_t n, size_t align)
{
    return (n + align - 1) & ~(align - 1);
}

gmx_status_t gmx_arena_init(gmx_arena_t *arena)
{
    if (!arena) return GMX_ERR_BAD_STATE;

    memset(arena, 0, sizeof(*arena));

    /* Pre-allocate first block */
    arena->blocks[0].data = malloc(GMX_ARENA_BLOCK_SIZE);
    if (!arena->blocks[0].data) return GMX_ERR_ALLOC;

    arena->blocks[0].size = GMX_ARENA_BLOCK_SIZE;
    arena->blocks[0].used = 0;
    arena->block_count    = 1;
    arena->current_block  = 0;
    arena->total_allocated = GMX_ARENA_BLOCK_SIZE;

    return GMX_OK;
}

void gmx_arena_destroy(gmx_arena_t *arena)
{
    if (!arena) return;
    for (size_t i = 0; i < arena->block_count; i++) {
        free(arena->blocks[i].data);
    }
    memset(arena, 0, sizeof(*arena));
}

void *gmx_arena_alloc(gmx_arena_t *arena, size_t size)
{
    if (!arena || size == 0) return NULL;

    size_t aligned_size = arena_align_up(size, GMX_ARENA_ALIGN);
    gmx_arena_block_t *blk = &arena->blocks[arena->current_block];

    /* Try current block */
    if (blk->used + aligned_size <= blk->size) {
        void *ptr = blk->data + blk->used;
        blk->used += aligned_size;
        arena->total_used += aligned_size;
        if (arena->total_used > arena->peak_used) {
            arena->peak_used = arena->total_used;
        }
        memset(ptr, 0, size);
        return ptr;
    }

    /* Need new block */
    if (arena->block_count >= GMX_ARENA_MAX_BLOCKS) return NULL;

    size_t new_size = GMX_ARENA_BLOCK_SIZE;
    if (aligned_size > new_size) {
        new_size = arena_align_up(aligned_size, GMX_ARENA_BLOCK_SIZE);
    }

    gmx_arena_block_t *new_blk = &arena->blocks[arena->block_count];
    new_blk->data = malloc(new_size);
    if (!new_blk->data) return NULL;

    new_blk->size = new_size;
    new_blk->used = aligned_size;
    arena->block_count++;
    arena->current_block = arena->block_count - 1;
    arena->total_allocated += new_size;
    arena->total_used += aligned_size;

    if (arena->total_used > arena->peak_used) {
        arena->peak_used = arena->total_used;
    }

    memset(new_blk->data, 0, size);
    return new_blk->data;
}

void gmx_arena_reset(gmx_arena_t *arena)
{
    if (!arena) return;

    /* Reset all blocks but keep the memory */
    for (size_t i = 0; i < arena->block_count; i++) {
        arena->blocks[i].used = 0;
    }
    arena->current_block = 0;
    arena->total_used    = 0;
}

size_t gmx_arena_bytes_used(const gmx_arena_t *arena)
{
    return arena ? arena->total_used : 0;
}

size_t gmx_arena_bytes_allocated(const gmx_arena_t *arena)
{
    return arena ? arena->total_allocated : 0;
}

size_t gmx_arena_peak_used(const gmx_arena_t *arena)
{
    return arena ? arena->peak_used : 0;
}

/*
 * Duplicate a byte buffer into arena-managed memory.
 */
void *gmx_arena_memdup(gmx_arena_t *arena, const void *src, size_t len)
{
    if (!arena || !src || len == 0) return NULL;
    void *dst = gmx_arena_alloc(arena, len);
    if (dst) {
        memcpy(dst, src, len);
    }
    return dst;
}

/*
 * Duplicate a NUL-terminated string into arena-managed memory.
 */
char *gmx_arena_strdup(gmx_arena_t *arena, const char *src)
{
    if (!arena || !src) return NULL;
    size_t len = strlen(src) + 1;
    char *dst = gmx_arena_alloc(arena, len);
    if (dst) {
        memcpy(dst, src, len);
    }
    return dst;
}
