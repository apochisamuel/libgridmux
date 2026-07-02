/*
 * libgridmux - Subrecord-level operations
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Higher-level operations on parsed subrecords: extraction,
 * validation, and transformation of subrecord payloads.
 */
#include "gmx/wire.h"
#include "gmx/crc.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

const gmx_subrecord_t *gmx_subrecord_find_by_kind(
    const gmx_subrecord_t *subs,
    uint16_t count,
    gmx_subrecord_kind_t kind)
{
    for (uint16_t i = 0; i < count; i++) {
        if (subs[i].kind == kind) return &subs[i];
    }
    return NULL;
}

uint16_t gmx_subrecord_count_kind(const gmx_subrecord_t *subs,
                                    uint16_t count,
                                    gmx_subrecord_kind_t kind)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (subs[i].kind == kind) n++;
    }
    return n;
}

gmx_status_t gmx_subrecord_validate_payload_crc(
    const gmx_subrecord_t *sr)
{
    if (!sr || !sr->payload || sr->payload_length < 4)
        return GMX_ERR_BAD_LENGTH;

    uint32_t stored_crc = gmx_wire_read_u32(
        sr->payload + sr->payload_length - 4);
    uint32_t computed = gmx_crc32c(0, sr->payload, sr->payload_length - 4);

    return computed == stored_crc ? GMX_OK : GMX_ERR_CRC_MISMATCH;
}

gmx_status_t gmx_subrecord_extract_u32(const gmx_subrecord_t *sr,
                                          size_t offset,
                                          uint32_t *out)
{
    if (!sr || !out) return GMX_ERR_BAD_STATE;
    if (!sr->payload || offset + 4 > sr->payload_length)
        return GMX_ERR_TRUNCATED;
    *out = gmx_wire_read_u32(sr->payload + offset);
    return GMX_OK;
}

gmx_status_t gmx_subrecord_extract_u16(const gmx_subrecord_t *sr,
                                          size_t offset,
                                          uint16_t *out)
{
    if (!sr || !out) return GMX_ERR_BAD_STATE;
    if (!sr->payload || offset + 2 > sr->payload_length)
        return GMX_ERR_TRUNCATED;
    *out = gmx_wire_read_u16(sr->payload + offset);
    return GMX_OK;
}

size_t gmx_subrecord_total_payload_bytes(const gmx_subrecord_t *subs,
                                           uint16_t count)
{
    size_t total = 0;
    for (uint16_t i = 0; i < count; i++) {
        total += subs[i].payload_length;
    }
    return total;
}

gmx_status_t gmx_subrecord_merge_payloads(const gmx_subrecord_t *subs,
                                             uint16_t count,
                                             gmx_subrecord_kind_t kind,
                                             uint8_t *dst,
                                             size_t dst_cap,
                                             size_t *out_len)
{
    if (!subs || !dst || !out_len) return GMX_ERR_BAD_STATE;
    size_t total = 0;

    for (uint16_t i = 0; i < count; i++) {
        if (subs[i].kind != kind) continue;
        if (!subs[i].payload) continue;
        if (total + subs[i].payload_length > dst_cap) break;
        memcpy(dst + total, subs[i].payload, subs[i].payload_length);
        total += subs[i].payload_length;
    }

    *out_len = total;
    return GMX_OK;
}

bool gmx_subrecord_has_lease_token(const gmx_subrecord_t *sr,
                                     uint32_t expected_token)
{
    return sr && sr->lease_token == expected_token;
}

bool gmx_subrecord_epoch_in_range(const gmx_subrecord_t *sr,
                                    uint32_t min_epoch,
                                    uint32_t max_epoch)
{
    if (!sr) return false;
    return sr->local_epoch >= min_epoch && sr->local_epoch <= max_epoch;
}
