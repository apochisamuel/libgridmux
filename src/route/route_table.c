/*
 * libgridmux - Route table maintenance and optimization
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Higher-level route table operations: rehashing, statistics,
 * topology analysis, and batch operations for migration.
 */
#include "gmx/route.h"
#include "gmx/crc.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

typedef struct gmx_route_table_stats {
    uint32_t active_entries;
    uint32_t inactive_entries;
    uint32_t total_subscribers;
    uint32_t max_subscribers;
    uint32_t avg_segment_len;
    uint32_t total_segment_bytes;
    float    load_factor;
} gmx_route_table_stats_t;

gmx_status_t gmx_route_table_stats(const gmx_route_table_t *table,
                                      gmx_route_table_stats_t *stats)
{
    if (!table || !stats) return GMX_ERR_BAD_STATE;
    memset(stats, 0, sizeof(*stats));

    uint64_t total_seg = 0;
    uint32_t seg_count = 0;

    for (size_t i = 0; i < table->capacity; i++) {
        gmx_route_entry_t *e = table->entries[i];
        if (!e) continue;

        if (e->active) {
            stats->active_entries++;
            stats->total_subscribers += e->subscriber_count;
            if (e->subscriber_count > stats->max_subscribers) {
                stats->max_subscribers = e->subscriber_count;
            }
            if (e->segment_len > 0) {
                total_seg += e->segment_len;
                seg_count++;
                stats->total_segment_bytes += (uint32_t)e->segment_len;
            }
        } else {
            stats->inactive_entries++;
        }
    }

    if (seg_count > 0) {
        stats->avg_segment_len = (uint32_t)(total_seg / seg_count);
    }
    stats->load_factor = table->capacity > 0 ?
        (float)table->count / (float)table->capacity : 0.0f;

    return GMX_OK;
}

size_t gmx_route_table_collect_active(const gmx_route_table_t *table,
                                        uint32_t *ids, size_t max_ids)
{
    if (!table || !ids || max_ids == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < table->capacity && count < max_ids; i++) {
        gmx_route_entry_t *e = table->entries[i];
        if (e && e->active) ids[count++] = e->route_id;
    }
    return count;
}

size_t gmx_route_table_collect_by_epoch(const gmx_route_table_t *table,
                                          uint32_t min_epoch,
                                          uint32_t max_epoch,
                                          uint32_t *ids, size_t max_ids)
{
    if (!table || !ids || max_ids == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < table->capacity && count < max_ids; i++) {
        gmx_route_entry_t *e = table->entries[i];
        if (e && e->active &&
            e->epoch_created >= min_epoch &&
            e->epoch_created <= max_epoch) {
            ids[count++] = e->route_id;
        }
    }
    return count;
}

uint32_t gmx_route_table_compute_topology_hash(const gmx_route_table_t *table)
{
    if (!table) return 0;
    uint32_t hash = 0;
    for (size_t i = 0; i < table->capacity; i++) {
        gmx_route_entry_t *e = table->entries[i];
        if (!e || !e->active) continue;

        uint8_t entry_data[16];
        entry_data[0] = (uint8_t)(e->route_id & 0xFF);
        entry_data[1] = (uint8_t)((e->route_id >> 8) & 0xFF);
        entry_data[2] = (uint8_t)((e->route_id >> 16) & 0xFF);
        entry_data[3] = (uint8_t)((e->route_id >> 24) & 0xFF);
        entry_data[4] = (uint8_t)(e->segment_id & 0xFF);
        entry_data[5] = (uint8_t)((e->segment_id >> 8) & 0xFF);
        entry_data[6] = (uint8_t)((e->segment_id >> 16) & 0xFF);
        entry_data[7] = (uint8_t)((e->segment_id >> 24) & 0xFF);
        entry_data[8] = (uint8_t)(e->generation & 0xFF);
        entry_data[9] = (uint8_t)((e->generation >> 8) & 0xFF);
        entry_data[10] = (uint8_t)((e->generation >> 16) & 0xFF);
        entry_data[11] = (uint8_t)((e->generation >> 24) & 0xFF);
        entry_data[12] = e->metric;
        entry_data[13] = (uint8_t)(e->next_hop_port & 0xFF);
        entry_data[14] = (uint8_t)((e->next_hop_port >> 8) & 0xFF);
        entry_data[15] = (uint8_t)e->subscriber_count;

        hash = gmx_crc32c(hash, entry_data, 16);
    }
    return hash;
}

gmx_route_entry_t *gmx_route_table_find_by_next_hop(
    const gmx_route_table_t *table,
    uint32_t next_hop_addr)
{
    if (!table) return NULL;
    for (size_t i = 0; i < table->capacity; i++) {
        gmx_route_entry_t *e = table->entries[i];
        if (e && e->active && e->next_hop_addr == next_hop_addr) {
            return e;
        }
    }
    return NULL;
}

gmx_route_entry_t *gmx_route_table_find_lowest_metric(
    const gmx_route_table_t *table)
{
    if (!table) return NULL;
    gmx_route_entry_t *best = NULL;
    for (size_t i = 0; i < table->capacity; i++) {
        gmx_route_entry_t *e = table->entries[i];
        if (e && e->active) {
            if (!best || e->metric < best->metric) {
                best = e;
            }
        }
    }
    return best;
}

uint32_t gmx_route_table_count_by_metric(const gmx_route_table_t *table,
                                            uint8_t max_metric)
{
    if (!table) return 0;
    uint32_t count = 0;
    for (size_t i = 0; i < table->capacity; i++) {
        gmx_route_entry_t *e = table->entries[i];
        if (e && e->active && e->metric <= max_metric) count++;
    }
    return count;
}
