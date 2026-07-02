/*
 * libgridmux - Grid-topology routing
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/route.h"
#include <stdlib.h>
#include <string.h>

gmx_status_t gmx_route_table_init(gmx_route_table_t *table, size_t capacity)
{
    if (!table || capacity == 0) {
        return GMX_ERR_BAD_STATE;
    }
    table->entries = calloc(capacity, sizeof(gmx_route_entry_t *));
    if (!table->entries) {
        return GMX_ERR_ALLOC;
    }
    table->capacity         = capacity;
    table->count            = 0;
    table->cascade_seq      = 0;
    table->table_generation = 1;
    return GMX_OK;
}

void gmx_route_table_destroy(gmx_route_table_t *table)
{
    if (!table || !table->entries) return;

    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i]) {
            free(table->entries[i]->segment_data);
            free(table->entries[i]);
        }
    }
    free(table->entries);
    table->entries = NULL;
    table->count = 0;
}

static size_t route_slot(gmx_route_table_t *table, uint32_t route_id)
{
    return (size_t)(route_id % table->capacity);
}

gmx_route_entry_t *gmx_route_entry_create(gmx_route_table_t *table,
                                            uint32_t route_id,
                                            uint32_t segment_id,
                                            uint32_t epoch,
                                            const uint8_t *data,
                                            size_t data_len)
{
    if (!table || table->count >= table->capacity) {
        return NULL;
    }

    size_t slot = route_slot(table, route_id);
    /* Linear probe for empty slot */
    size_t orig = slot;
    while (table->entries[slot] != NULL) {
        if (table->entries[slot]->route_id == route_id &&
            table->entries[slot]->active) {
            return NULL;  /* duplicate */
        }
        slot = (slot + 1) % table->capacity;
        if (slot == orig) return NULL;  /* full */
    }

    gmx_route_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry) return NULL;

    entry->route_id    = route_id;
    entry->segment_id  = segment_id;
    entry->generation  = table->table_generation++;
    entry->epoch_created = epoch;
    entry->active      = true;
    entry->metric      = 1;

    if (data && data_len > 0 && data_len <= GMX_MAX_PAYLOAD) {
        entry->segment_data = malloc(data_len);
        if (!entry->segment_data) {
            free(entry);
            return NULL;
        }
        memcpy(entry->segment_data, data, data_len);
        entry->segment_len = data_len;
    }

    /* Derive next-hop from segment data */
    if (entry->segment_data && entry->segment_len >= 6) {
        entry->next_hop_addr = (uint32_t)entry->segment_data[0]
                             | ((uint32_t)entry->segment_data[1] << 8)
                             | ((uint32_t)entry->segment_data[2] << 16)
                             | ((uint32_t)entry->segment_data[3] << 24);
        entry->next_hop_port = (uint16_t)entry->segment_data[4]
                             | ((uint16_t)entry->segment_data[5] << 8);
    }

    table->entries[slot] = entry;
    table->count++;
    return entry;
}

gmx_route_entry_t *gmx_route_table_lookup(gmx_route_table_t *table,
                                            uint32_t route_id)
{
    if (!table) return NULL;

    size_t slot = route_slot(table, route_id);
    size_t orig = slot;
    do {
        gmx_route_entry_t *e = table->entries[slot];
        if (!e) return NULL;
        if (e->route_id == route_id && e->active) {
            return e;
        }
        slot = (slot + 1) % table->capacity;
    } while (slot != orig);
    return NULL;
}

gmx_status_t gmx_route_table_remove(gmx_route_table_t *table,
                                      uint32_t route_id)
{
    if (!table) return GMX_ERR_BAD_STATE;

    size_t slot = route_slot(table, route_id);
    size_t orig = slot;
    do {
        gmx_route_entry_t *e = table->entries[slot];
        if (!e) return GMX_ERR_NOT_FOUND;
        if (e->route_id == route_id && e->active) {
            e->active = false;
            return GMX_OK;
        }
        slot = (slot + 1) % table->capacity;
    } while (slot != orig);
    return GMX_ERR_NOT_FOUND;
}

gmx_status_t gmx_route_cascade_update(gmx_route_table_t *table,
                                        uint32_t route_id,
                                        const uint8_t *new_data,
                                        size_t new_len)
{
    if (!table) return GMX_ERR_BAD_STATE;

    /*
     * Cascade update replaces the route entry with a new version.
     * The old entry is freed and a fresh entry is created at the
     * same table slot with updated segment data and generation.
     */
    for (size_t i = 0; i < table->capacity; i++) {
        gmx_route_entry_t *entry = table->entries[i];
        if (!entry || entry->route_id != route_id || !entry->active)
            continue;

        /* Save metadata from old entry */
        uint32_t seg_id = entry->segment_id;
        uint32_t epoch  = entry->epoch_created;

        /* Free the old entry completely */
        free(entry->segment_data);
        free(entry);
        table->entries[i] = NULL;
        table->count--;

        /* Create replacement entry with new data */
        gmx_route_entry_t *replacement = gmx_route_entry_create(
            table, route_id, seg_id, epoch, new_data, new_len);
        if (!replacement) return GMX_ERR_ALLOC;

        replacement->generation = table->table_generation++;
        table->cascade_seq++;
        return GMX_OK;
    }

    return GMX_ERR_NOT_FOUND;
}
