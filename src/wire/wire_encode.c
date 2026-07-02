/*
 * libgridmux - Wire protocol encoding
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/wire.h"
#include "gmx/crc.h"
#include <string.h>

/*
 * Encode a session header into a buffer.
 * Returns the number of bytes written, or 0 on error.
 */
size_t gmx_wire_encode_session_header(uint8_t *dst, size_t dst_cap,
                                       uint32_t session_id,
                                       uint32_t epoch,
                                       uint16_t frame_count)
{
    if (!dst || dst_cap < GMX_SESSION_HEADER_WIRE_SIZE) return 0;

    size_t off = 0;
    gmx_wire_write_u32(dst + off, GMX_MAGIC);       off += 4;
    gmx_wire_write_u16(dst + off, GMX_VERSION);      off += 2;
    gmx_wire_write_u32(dst + off, session_id);       off += 4;
    gmx_wire_write_u32(dst + off, epoch);            off += 4;
    gmx_wire_write_u16(dst + off, frame_count);      off += 2;

    /* CRC covers first 16 bytes */
    uint32_t crc = gmx_crc32c(0, dst, 16);
    gmx_wire_write_u32(dst + off, crc);              off += 4;

    return off;
}

/*
 * Encode a subrecord into a buffer.
 * Returns bytes written or 0 on error.
 */
size_t gmx_wire_encode_subrecord(uint8_t *dst, size_t dst_cap,
                                   uint16_t kind,
                                   uint32_t local_epoch,
                                   uint32_t generation,
                                   uint32_t lease_token,
                                   const uint8_t *payload,
                                   uint16_t payload_len)
{
    uint16_t total = GMX_SUBRECORD_HEADER_WIRE_SIZE + payload_len;
    if (!dst || dst_cap < total) return 0;

    size_t off = 0;
    gmx_wire_write_u16(dst + off, kind);         off += 2;
    gmx_wire_write_u16(dst + off, total);        off += 2;
    gmx_wire_write_u32(dst + off, local_epoch);  off += 4;
    gmx_wire_write_u32(dst + off, generation);   off += 4;
    gmx_wire_write_u32(dst + off, lease_token);  off += 4;

    if (payload && payload_len > 0) {
        memcpy(dst + off, payload, payload_len);
        off += payload_len;
    }

    return off;
}

/*
 * Encode a complete frame with one subrecord.
 * Returns bytes written or 0 on error.
 */
size_t gmx_wire_encode_frame(uint8_t *dst, size_t dst_cap,
                               uint16_t frame_type,
                               uint32_t sequence,
                               uint32_t session_id,
                               uint32_t channel_id,
                               uint32_t route_id,
                               uint32_t owner_token,
                               uint16_t flags,
                               uint16_t sub_kind,
                               uint32_t sub_epoch,
                               uint32_t sub_gen,
                               uint32_t sub_lease,
                               const uint8_t *sub_payload,
                               uint16_t sub_payload_len)
{
    /* Build subrecord first in temp buffer */
    uint8_t sub_buf[GMX_MAX_PAYLOAD];
    size_t sub_len = gmx_wire_encode_subrecord(
        sub_buf, sizeof(sub_buf),
        sub_kind, sub_epoch, sub_gen, sub_lease,
        sub_payload, sub_payload_len);
    if (sub_len == 0) return 0;

    uint32_t total_len = GMX_FRAME_HEADER_WIRE_SIZE + (uint32_t)sub_len;
    if (dst_cap < total_len) return 0;

    /* Compute payload CRC */
    uint32_t payload_crc = gmx_crc32c(0, sub_buf, sub_len);

    /* Write frame header */
    size_t off = 0;
    gmx_wire_write_u32(dst + off, total_len);          off += 4;
    gmx_wire_write_u16(dst + off, frame_type);         off += 2;
    gmx_wire_write_u32(dst + off, sequence);           off += 4;
    gmx_wire_write_u32(dst + off, session_id);         off += 4;
    gmx_wire_write_u32(dst + off, channel_id);         off += 4;
    gmx_wire_write_u32(dst + off, route_id);           off += 4;
    gmx_wire_write_u32(dst + off, owner_token);        off += 4;
    gmx_wire_write_u16(dst + off, flags);              off += 2;
    gmx_wire_write_u16(dst + off, 1);                  off += 2; /* 1 subrecord */
    gmx_wire_write_u32(dst + off, payload_crc);        off += 4;

    /* Write subrecord body */
    memcpy(dst + off, sub_buf, sub_len);
    off += sub_len;

    return off;
}

/*
 * Encode a frame with multiple subrecords.
 * Returns bytes written or 0 on error.
 */
size_t gmx_wire_encode_frame_multi(uint8_t *dst, size_t dst_cap,
                                     uint16_t frame_type,
                                     uint32_t sequence,
                                     uint32_t session_id,
                                     uint32_t channel_id,
                                     uint32_t route_id,
                                     uint32_t owner_token,
                                     uint16_t flags,
                                     const gmx_subrecord_t *subs,
                                     uint16_t sub_count)
{
    if (!subs || sub_count == 0 || sub_count > GMX_MAX_SUBRECORDS) return 0;

    /* Encode all subrecords into temp buffer */
    uint8_t sub_buf[GMX_MAX_FRAME_LEN];
    size_t sub_total = 0;

    for (uint16_t i = 0; i < sub_count; i++) {
        size_t n = gmx_wire_encode_subrecord(
            sub_buf + sub_total, sizeof(sub_buf) - sub_total,
            subs[i].kind, subs[i].local_epoch,
            subs[i].generation, subs[i].lease_token,
            subs[i].payload, subs[i].payload_length);
        if (n == 0) return 0;
        sub_total += n;
    }

    uint32_t total_len = GMX_FRAME_HEADER_WIRE_SIZE + (uint32_t)sub_total;
    if (dst_cap < total_len) return 0;

    uint32_t payload_crc = gmx_crc32c(0, sub_buf, sub_total);

    size_t off = 0;
    gmx_wire_write_u32(dst + off, total_len);          off += 4;
    gmx_wire_write_u16(dst + off, frame_type);         off += 2;
    gmx_wire_write_u32(dst + off, sequence);           off += 4;
    gmx_wire_write_u32(dst + off, session_id);         off += 4;
    gmx_wire_write_u32(dst + off, channel_id);         off += 4;
    gmx_wire_write_u32(dst + off, route_id);           off += 4;
    gmx_wire_write_u32(dst + off, owner_token);        off += 4;
    gmx_wire_write_u16(dst + off, flags);              off += 2;
    gmx_wire_write_u16(dst + off, sub_count);          off += 2;
    gmx_wire_write_u32(dst + off, payload_crc);        off += 4;

    memcpy(dst + off, sub_buf, sub_total);
    off += sub_total;

    return off;
}

/*
 * Build a forwarding descriptor for wire transmission.
 * Encodes route information and stream data into a compact format.
 */
size_t gmx_wire_build_forward_descriptor(uint8_t *dst, size_t dst_cap,
                                           uint32_t next_hop_addr,
                                           uint32_t route_id,
                                           uint32_t segment_id,
                                           const uint8_t *seg_data,
                                           size_t seg_len)
{
    size_t needed = 12 + seg_len;
    if (!dst || dst_cap < needed) return 0;

    size_t off = 0;
    gmx_wire_write_u32(dst + off, next_hop_addr);  off += 4;
    gmx_wire_write_u32(dst + off, route_id);       off += 4;
    gmx_wire_write_u32(dst + off, segment_id);     off += 4;

    if (seg_data && seg_len > 0) {
        memcpy(dst + off, seg_data, seg_len);
        off += seg_len;
    }

    return off;
}

/*
 * Compute frame integrity check value combining
 * sequence, session_id, and payload CRC.
 */
uint32_t gmx_wire_frame_integrity(uint32_t sequence, uint32_t session_id,
                                    uint32_t payload_crc)
{
    uint8_t buf[12];
    gmx_wire_write_u32(buf, sequence);
    gmx_wire_write_u32(buf + 4, session_id);
    gmx_wire_write_u32(buf + 8, payload_crc);
    return gmx_crc32c(0, buf, 12);
}

/*
 * Validate frame sequence ordering. Returns GMX_OK if the
 * frame sequence is valid for the given expected value.
 */
gmx_status_t gmx_wire_validate_sequence(uint32_t frame_seq,
                                          uint32_t expected_seq)
{
    return frame_seq == expected_seq ? GMX_OK : GMX_ERR_BAD_SEQUENCE;
}
