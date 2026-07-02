/*
 * libgridmux - Wire protocol decoding
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/wire.h"
#include "gmx/crc.h"
#include <string.h>

uint16_t gmx_wire_read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t gmx_wire_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

void gmx_wire_write_u16(uint8_t *p, uint16_t val)
{
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
}

void gmx_wire_write_u32(uint8_t *p, uint32_t val)
{
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

gmx_status_t gmx_wire_decode_session_header(const uint8_t *data, size_t len,
                                             gmx_session_header_t *out)
{
    if (len < GMX_SESSION_HEADER_WIRE_SIZE) {
        return GMX_ERR_TRUNCATED;
    }

    size_t off = 0;
    out->magic       = gmx_wire_read_u32(data + off); off += 4;
    out->version     = gmx_wire_read_u16(data + off); off += 2;
    out->session_id  = gmx_wire_read_u32(data + off); off += 4;
    out->epoch       = gmx_wire_read_u32(data + off); off += 4;
    out->frame_count = gmx_wire_read_u16(data + off); off += 2;
    out->header_crc  = gmx_wire_read_u32(data + off);

    if (out->magic != GMX_MAGIC) {
        return GMX_ERR_INVALID_MAGIC;
    }

    if (out->version != GMX_VERSION) {
        return GMX_ERR_INVALID_VERSION;
    }

    /* Validate CRC over bytes before the CRC field (16 bytes) */
    uint32_t computed = gmx_crc32c(0, data, 16);
    if (computed != out->header_crc) {
        return GMX_ERR_CRC_MISMATCH;
    }

    if (out->frame_count == 0 || out->frame_count > GMX_MAX_FRAME_COUNT) {
        return GMX_ERR_BAD_LENGTH;
    }

    return GMX_OK;
}

gmx_status_t gmx_wire_decode_subrecords(const uint8_t *data, size_t len,
                                         gmx_subrecord_t *out,
                                         uint16_t max_count,
                                         uint16_t *actual_count)
{
    size_t off = 0;
    uint16_t count = 0;

    while (off + GMX_SUBRECORD_HEADER_WIRE_SIZE <= len && count < max_count) {
        gmx_subrecord_t *sr = &out[count];

        sr->kind         = gmx_wire_read_u16(data + off);      off += 2;
        sr->total_length = gmx_wire_read_u16(data + off);      off += 2;
        sr->local_epoch  = gmx_wire_read_u32(data + off);      off += 4;
        sr->generation   = gmx_wire_read_u32(data + off);      off += 4;
        sr->lease_token  = gmx_wire_read_u32(data + off);      off += 4;

        if (sr->total_length < GMX_SUBRECORD_HEADER_WIRE_SIZE) {
            return GMX_ERR_BAD_LENGTH;
        }

        sr->payload_length = sr->total_length - GMX_SUBRECORD_HEADER_WIRE_SIZE;

        if (off + sr->payload_length > len) {
            return GMX_ERR_TRUNCATED;
        }

        if (sr->payload_length > 0) {
            sr->payload = data + off;
        } else {
            sr->payload = NULL;
        }
        off += sr->payload_length;

        if (sr->kind == 0 || sr->kind >= GMX_SUB_KIND_MAX) {
            return GMX_ERR_BAD_SUBRECORD;
        }

        count++;
    }

    *actual_count = count;
    return GMX_OK;
}

gmx_status_t gmx_wire_decode_frame(const uint8_t *data, size_t len,
                                    gmx_frame_t *out, size_t *consumed)
{
    if (len < GMX_FRAME_HEADER_WIRE_SIZE) {
        return GMX_ERR_TRUNCATED;
    }

    memset(out, 0, sizeof(*out));

    size_t off = 0;
    out->header.length          = gmx_wire_read_u32(data + off); off += 4;
    out->header.frame_type      = gmx_wire_read_u16(data + off); off += 2;
    out->header.sequence        = gmx_wire_read_u32(data + off); off += 4;
    out->header.session_id      = gmx_wire_read_u32(data + off); off += 4;
    out->header.channel_id      = gmx_wire_read_u32(data + off); off += 4;
    out->header.route_id        = gmx_wire_read_u32(data + off); off += 4;
    out->header.owner_token     = gmx_wire_read_u32(data + off); off += 4;
    out->header.flags           = gmx_wire_read_u16(data + off); off += 2;
    out->header.subrecord_count = gmx_wire_read_u16(data + off); off += 2;
    out->header.payload_crc     = gmx_wire_read_u32(data + off); off += 4;

    if (out->header.length < GMX_FRAME_HEADER_WIRE_SIZE ||
        out->header.length > GMX_MAX_FRAME_LEN) {
        return GMX_ERR_BAD_LENGTH;
    }

    if (out->header.length > len) {
        return GMX_ERR_TRUNCATED;
    }

    if (out->header.frame_type == 0 ||
        out->header.frame_type >= GMX_FRAME_TYPE_MAX) {
        return GMX_ERR_BAD_FRAME_TYPE;
    }

    if (out->header.subrecord_count > GMX_MAX_SUBRECORDS) {
        return GMX_ERR_BAD_LENGTH;
    }

    /* Validate payload CRC (covers bytes after header) */
    size_t payload_len = out->header.length - GMX_FRAME_HEADER_WIRE_SIZE;
    if (payload_len > 0) {
        uint32_t computed = gmx_crc32c(0, data + GMX_FRAME_HEADER_WIRE_SIZE,
                                        payload_len);
        if (computed != out->header.payload_crc) {
            return GMX_ERR_CRC_MISMATCH;
        }
    }

    out->raw        = data;
    out->raw_length = out->header.length;

    /* Decode subrecords from payload */
    if (out->header.subrecord_count > 0 && payload_len > 0) {
        gmx_status_t rc = gmx_wire_decode_subrecords(
            data + GMX_FRAME_HEADER_WIRE_SIZE,
            payload_len,
            out->subrecords,
            out->header.subrecord_count,
            &out->subrecord_count);
        if (rc != GMX_OK) {
            return rc;
        }
        if (out->subrecord_count != out->header.subrecord_count) {
            return GMX_ERR_BAD_SUBRECORD;
        }
    }

    *consumed = out->header.length;
    return GMX_OK;
}

bool gmx_wire_validate_frame_crc(const gmx_frame_t *frame)
{
    if (!frame || !frame->raw || frame->raw_length < GMX_FRAME_HEADER_WIRE_SIZE) {
        return false;
    }
    size_t payload_len = frame->raw_length - GMX_FRAME_HEADER_WIRE_SIZE;
    if (payload_len == 0) {
        return true;
    }
    uint32_t computed = gmx_crc32c(0,
        frame->raw + GMX_FRAME_HEADER_WIRE_SIZE, payload_len);
    return computed == frame->header.payload_crc;
}
