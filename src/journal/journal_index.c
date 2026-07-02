/*
 * libgridmux - Journal segment index for efficient lookup
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Maintains a secondary index over journal segments for efficient
 * epoch-range queries, stream-filtered lookups, and checkpoint
 * discovery. The index is rebuilt on demand after truncation.
 */
#include "gmx/journal.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_JOURNAL_INDEX_SIZE  128

typedef struct gmx_journal_index_entry {
    uint32_t segment_id;
    uint32_t epoch;
    uint32_t generation;
    uint32_t offset_base;
    size_t   data_len;
    bool     sealed;
    bool     valid;
} gmx_journal_index_entry_t;

typedef struct gmx_journal_index {
    gmx_journal_index_entry_t entries[GMX_JOURNAL_INDEX_SIZE];
    uint32_t count;
    uint32_t min_epoch;
    uint32_t max_epoch;
    bool     sorted;
} gmx_journal_index_t;

gmx_status_t gmx_journal_index_init(gmx_journal_index_t *idx)
{
    if (!idx) return GMX_ERR_BAD_STATE;
    memset(idx, 0, sizeof(*idx));
    idx->min_epoch = 0xFFFFFFFF;
    return GMX_OK;
}

gmx_status_t gmx_journal_index_build(gmx_journal_index_t *idx,
                                        const gmx_journal_t *j)
{
    if (!idx || !j) return GMX_ERR_BAD_STATE;

    idx->count = 0;
    idx->min_epoch = 0xFFFFFFFF;
    idx->max_epoch = 0;
    idx->sorted = false;

    for (size_t i = 0; i < j->capacity && idx->count < GMX_JOURNAL_INDEX_SIZE; i++) {
        gmx_journal_segment_t *seg = j->segments[i];
        if (!seg || !seg->active) continue;

        gmx_journal_index_entry_t *e = &idx->entries[idx->count];
        e->segment_id  = seg->segment_id;
        e->epoch       = seg->epoch_created;
        e->generation  = seg->generation;
        e->offset_base = seg->offset_base;
        e->data_len    = seg->data_len;
        e->sealed      = seg->sealed;
        e->valid       = true;

        if (e->epoch < idx->min_epoch) idx->min_epoch = e->epoch;
        if (e->epoch > idx->max_epoch) idx->max_epoch = e->epoch;

        idx->count++;
    }

    /* Sort by epoch ascending, then by generation */
    for (uint32_t i = 0; i < idx->count; i++) {
        for (uint32_t k = i + 1; k < idx->count; k++) {
            bool swap = false;
            if (idx->entries[k].epoch < idx->entries[i].epoch) {
                swap = true;
            } else if (idx->entries[k].epoch == idx->entries[i].epoch &&
                       idx->entries[k].generation < idx->entries[i].generation) {
                swap = true;
            }
            if (swap) {
                gmx_journal_index_entry_t tmp = idx->entries[i];
                idx->entries[i] = idx->entries[k];
                idx->entries[k] = tmp;
            }
        }
    }
    idx->sorted = true;

    return GMX_OK;
}

uint32_t gmx_journal_index_count_in_epoch_range(
    const gmx_journal_index_t *idx,
    uint32_t min_epoch,
    uint32_t max_epoch)
{
    if (!idx) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < idx->count; i++) {
        if (idx->entries[i].valid &&
            idx->entries[i].epoch >= min_epoch &&
            idx->entries[i].epoch <= max_epoch) {
            count++;
        }
    }
    return count;
}

size_t gmx_journal_index_bytes_in_range(const gmx_journal_index_t *idx,
                                          uint32_t min_epoch,
                                          uint32_t max_epoch)
{
    if (!idx) return 0;
    size_t total = 0;
    for (uint32_t i = 0; i < idx->count; i++) {
        if (idx->entries[i].valid &&
            idx->entries[i].epoch >= min_epoch &&
            idx->entries[i].epoch <= max_epoch) {
            total += idx->entries[i].data_len;
        }
    }
    return total;
}

const gmx_journal_index_entry_t *gmx_journal_index_find_segment(
    const gmx_journal_index_t *idx,
    uint32_t segment_id)
{
    if (!idx) return NULL;
    for (uint32_t i = 0; i < idx->count; i++) {
        if (idx->entries[i].valid &&
            idx->entries[i].segment_id == segment_id) {
            return &idx->entries[i];
        }
    }
    return NULL;
}

const gmx_journal_index_entry_t *gmx_journal_index_newest_sealed(
    const gmx_journal_index_t *idx)
{
    if (!idx || idx->count == 0) return NULL;
    for (int i = (int)idx->count - 1; i >= 0; i--) {
        if (idx->entries[i].valid && idx->entries[i].sealed) {
            return &idx->entries[i];
        }
    }
    return NULL;
}

uint32_t gmx_journal_index_sealed_count(const gmx_journal_index_t *idx)
{
    if (!idx) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < idx->count; i++) {
        if (idx->entries[i].valid && idx->entries[i].sealed) count++;
    }
    return count;
}
