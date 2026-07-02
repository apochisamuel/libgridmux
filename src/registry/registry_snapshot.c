/*
 * libgridmux - Registry state snapshot for migration and recovery
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Captures a point-in-time snapshot of the registry for migration
 * transfer and crash recovery. Snapshots are serialized to a compact
 * binary format for wire transfer between sessions.
 */
#include "gmx/registry.h"
#include "gmx/crc.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_SNAPSHOT_HEADER_SIZE   16
#define GMX_SNAPSHOT_ENTRY_SIZE    20
#define GMX_SNAPSHOT_MAX_ENTRIES   256
#define GMX_SNAPSHOT_MAGIC         0x52454753u  /* "REGS" */

typedef struct gmx_registry_snapshot_header {
    uint32_t magic;
    uint32_t entry_count;
    uint32_t epoch;
    uint32_t checksum;
} gmx_registry_snapshot_header_t;

typedef struct gmx_registry_snapshot_entry {
    uint32_t id;
    uint32_t type;
    uint32_t generation;
    uint32_t epoch_created;
    uint32_t flags;
} gmx_registry_snapshot_entry_t;

typedef struct gmx_registry_snapshot {
    gmx_registry_snapshot_header_t header;
    gmx_registry_snapshot_entry_t  entries[GMX_SNAPSHOT_MAX_ENTRIES];
    uint32_t                       count;
    bool                           finalized;
} gmx_registry_snapshot_t;

gmx_status_t gmx_registry_snapshot_init(gmx_registry_snapshot_t *snap,
                                          uint32_t epoch)
{
    if (!snap) return GMX_ERR_BAD_STATE;
    memset(snap, 0, sizeof(*snap));
    snap->header.magic = GMX_SNAPSHOT_MAGIC;
    snap->header.epoch = epoch;
    return GMX_OK;
}

gmx_status_t gmx_registry_snapshot_capture(gmx_registry_snapshot_t *snap,
                                             const gmx_registry_t *reg)
{
    if (!snap || !reg) return GMX_ERR_BAD_STATE;
    if (snap->finalized) return GMX_ERR_BAD_STATE;

    snap->count = 0;
    for (size_t i = 0; i < reg->bucket_count; i++) {
        gmx_registry_entry_t *e = reg->buckets[i];
        while (e && snap->count < GMX_SNAPSHOT_MAX_ENTRIES) {
            if (e->active) {
                gmx_registry_snapshot_entry_t *se = &snap->entries[snap->count];
                se->id            = e->id;
                se->type          = e->type;
                se->generation    = e->generation;
                se->epoch_created = e->epoch_created;
                se->flags         = e->active ? 0x01 : 0x00;
                snap->count++;
            }
            e = e->next;
        }
    }

    snap->header.entry_count = snap->count;
    return GMX_OK;
}

gmx_status_t gmx_registry_snapshot_finalize(gmx_registry_snapshot_t *snap)
{
    if (!snap || snap->finalized) return GMX_ERR_BAD_STATE;

    /* Compute checksum over all entries */
    uint32_t crc = gmx_crc32c(0, (const uint8_t *)snap->entries,
                                snap->count * sizeof(gmx_registry_snapshot_entry_t));
    snap->header.checksum = crc;
    snap->finalized = true;
    return GMX_OK;
}

size_t gmx_registry_snapshot_serialize(const gmx_registry_snapshot_t *snap,
                                         uint8_t *dst, size_t dst_cap)
{
    if (!snap || !snap->finalized || !dst) return 0;

    size_t total = GMX_SNAPSHOT_HEADER_SIZE +
                   snap->count * GMX_SNAPSHOT_ENTRY_SIZE;
    if (dst_cap < total) return 0;

    size_t off = 0;
    memcpy(dst + off, &snap->header.magic, 4);         off += 4;
    memcpy(dst + off, &snap->header.entry_count, 4);   off += 4;
    memcpy(dst + off, &snap->header.epoch, 4);         off += 4;
    memcpy(dst + off, &snap->header.checksum, 4);      off += 4;

    for (uint32_t i = 0; i < snap->count; i++) {
        const gmx_registry_snapshot_entry_t *e = &snap->entries[i];
        memcpy(dst + off, &e->id, 4);            off += 4;
        memcpy(dst + off, &e->type, 4);          off += 4;
        memcpy(dst + off, &e->generation, 4);    off += 4;
        memcpy(dst + off, &e->epoch_created, 4); off += 4;
        memcpy(dst + off, &e->flags, 4);         off += 4;
    }

    return off;
}

gmx_status_t gmx_registry_snapshot_deserialize(gmx_registry_snapshot_t *snap,
                                                  const uint8_t *src,
                                                  size_t src_len)
{
    if (!snap || !src || src_len < GMX_SNAPSHOT_HEADER_SIZE)
        return GMX_ERR_TRUNCATED;

    memset(snap, 0, sizeof(*snap));

    size_t off = 0;
    memcpy(&snap->header.magic, src + off, 4);         off += 4;
    memcpy(&snap->header.entry_count, src + off, 4);   off += 4;
    memcpy(&snap->header.epoch, src + off, 4);         off += 4;
    memcpy(&snap->header.checksum, src + off, 4);      off += 4;

    if (snap->header.magic != GMX_SNAPSHOT_MAGIC) return GMX_ERR_INVALID_MAGIC;
    if (snap->header.entry_count > GMX_SNAPSHOT_MAX_ENTRIES)
        return GMX_ERR_OVERFLOW;

    size_t entries_size = snap->header.entry_count * GMX_SNAPSHOT_ENTRY_SIZE;
    if (src_len < GMX_SNAPSHOT_HEADER_SIZE + entries_size)
        return GMX_ERR_TRUNCATED;

    for (uint32_t i = 0; i < snap->header.entry_count; i++) {
        gmx_registry_snapshot_entry_t *e = &snap->entries[i];
        memcpy(&e->id, src + off, 4);            off += 4;
        memcpy(&e->type, src + off, 4);          off += 4;
        memcpy(&e->generation, src + off, 4);    off += 4;
        memcpy(&e->epoch_created, src + off, 4); off += 4;
        memcpy(&e->flags, src + off, 4);         off += 4;
    }

    snap->count = snap->header.entry_count;

    /* Verify checksum */
    uint32_t computed = gmx_crc32c(0, (const uint8_t *)snap->entries,
                                    snap->count * sizeof(gmx_registry_snapshot_entry_t));
    if (computed != snap->header.checksum) return GMX_ERR_CRC_MISMATCH;

    snap->finalized = true;
    return GMX_OK;
}

gmx_status_t gmx_registry_snapshot_apply(const gmx_registry_snapshot_t *snap,
                                            gmx_registry_t *reg)
{
    if (!snap || !reg || !snap->finalized) return GMX_ERR_BAD_STATE;

    for (uint32_t i = 0; i < snap->count; i++) {
        const gmx_registry_snapshot_entry_t *e = &snap->entries[i];
        if (e->flags & 0x01) {
            gmx_registry_insert(reg, e->id, e->type, NULL, e->epoch_created);
        }
    }
    return GMX_OK;
}

const gmx_registry_snapshot_entry_t *gmx_registry_snapshot_find(
    const gmx_registry_snapshot_t *snap,
    uint32_t id, uint32_t type)
{
    if (!snap) return NULL;
    for (uint32_t i = 0; i < snap->count; i++) {
        if (snap->entries[i].id == id && snap->entries[i].type == type) {
            return &snap->entries[i];
        }
    }
    return NULL;
}

uint32_t gmx_registry_snapshot_count_type(const gmx_registry_snapshot_t *snap,
                                             uint32_t type)
{
    if (!snap) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < snap->count; i++) {
        if (snap->entries[i].type == type) count++;
    }
    return count;
}

gmx_status_t gmx_registry_snapshot_diff(const gmx_registry_snapshot_t *old_snap,
                                           const gmx_registry_snapshot_t *new_snap,
                                           uint32_t *added, uint32_t *removed,
                                           uint32_t *changed)
{
    if (!old_snap || !new_snap || !added || !removed || !changed)
        return GMX_ERR_BAD_STATE;

    *added = 0;
    *removed = 0;
    *changed = 0;

    /* Count entries in new but not in old */
    for (uint32_t i = 0; i < new_snap->count; i++) {
        const gmx_registry_snapshot_entry_t *ne = &new_snap->entries[i];
        const gmx_registry_snapshot_entry_t *oe =
            gmx_registry_snapshot_find(old_snap, ne->id, ne->type);
        if (!oe) {
            (*added)++;
        } else if (oe->generation != ne->generation) {
            (*changed)++;
        }
    }

    /* Count entries in old but not in new */
    for (uint32_t i = 0; i < old_snap->count; i++) {
        const gmx_registry_snapshot_entry_t *oe = &old_snap->entries[i];
        const gmx_registry_snapshot_entry_t *ne =
            gmx_registry_snapshot_find(new_snap, oe->id, oe->type);
        if (!ne) {
            (*removed)++;
        }
    }

    return GMX_OK;
}
