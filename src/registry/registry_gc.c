/*
 * libgridmux - Registry garbage collection with epoch watermark
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/registry.h"
#include <stdlib.h>
#include <string.h>

typedef struct gmx_registry_gc_stats {
    uint32_t scanned;
    uint32_t collected;
    uint32_t retained;
    uint32_t watermark_epoch;
} gmx_registry_gc_stats_t;

gmx_status_t gmx_registry_gc_sweep(gmx_registry_t *reg,
                                     uint32_t watermark_epoch,
                                     uint32_t type_filter,
                                     gmx_registry_gc_stats_t *stats)
{
    if (!reg || !stats) return GMX_ERR_BAD_STATE;
    memset(stats, 0, sizeof(*stats));
    stats->watermark_epoch = watermark_epoch;

    for (size_t i = 0; i < reg->bucket_count; i++) {
        gmx_registry_entry_t **pp = &reg->buckets[i];
        while (*pp) {
            gmx_registry_entry_t *e = *pp;
            stats->scanned++;

            if (!e->active && e->epoch_created < watermark_epoch &&
                (type_filter == 0 || e->type == type_filter)) {
                *pp = e->next;
                free(e);
                reg->entry_count--;
                stats->collected++;
            } else {
                stats->retained++;
                pp = &e->next;
            }
        }
    }
    return GMX_OK;
}

gmx_status_t gmx_registry_gc_all_types(gmx_registry_t *reg,
                                          uint32_t watermark_epoch,
                                          gmx_registry_gc_stats_t *stats)
{
    return gmx_registry_gc_sweep(reg, watermark_epoch, 0, stats);
}

uint32_t gmx_registry_count_stale(const gmx_registry_t *reg,
                                    uint32_t watermark_epoch)
{
    if (!reg) return 0;
    uint32_t count = 0;
    for (size_t i = 0; i < reg->bucket_count; i++) {
        gmx_registry_entry_t *e = reg->buckets[i];
        while (e) {
            if (!e->active && e->epoch_created < watermark_epoch) count++;
            e = e->next;
        }
    }
    return count;
}

uint32_t gmx_registry_count_active(const gmx_registry_t *reg, uint32_t type)
{
    if (!reg) return 0;
    uint32_t count = 0;
    for (size_t i = 0; i < reg->bucket_count; i++) {
        gmx_registry_entry_t *e = reg->buckets[i];
        while (e) {
            if (e->active && (type == 0 || e->type == type)) count++;
            e = e->next;
        }
    }
    return count;
}

float gmx_registry_load_factor(const gmx_registry_t *reg)
{
    if (!reg || reg->bucket_count == 0) return 0.0f;
    return (float)reg->entry_count / (float)reg->bucket_count;
}

gmx_status_t gmx_registry_compact_buckets(gmx_registry_t *reg)
{
    if (!reg) return GMX_ERR_BAD_STATE;
    for (size_t i = 0; i < reg->bucket_count; i++) {
        gmx_registry_entry_t **pp = &reg->buckets[i];
        while (*pp) {
            if (!(*pp)->active) {
                gmx_registry_entry_t *dead = *pp;
                *pp = dead->next;
                free(dead);
                reg->entry_count--;
            } else {
                pp = &(*pp)->next;
            }
        }
    }
    return GMX_OK;
}
