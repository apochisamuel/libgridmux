/*
 * libgridmux - Route segment data management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Route segments carry topology data that describes how to
 * forward data through the mesh. Segments are immutable once
 * installed; updates create new segments with new generation
 * numbers.
 */
#include "gmx/route.h"
#include "gmx/crc.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_ROUTE_SEG_HEADER_SIZE  12

typedef struct gmx_route_seg_header {
    uint32_t next_hop_addr;
    uint16_t next_hop_port;
    uint8_t  metric;
    uint8_t  flags;
    uint32_t checksum;
} gmx_route_seg_header_t;

gmx_status_t gmx_route_segment_parse(const uint8_t *data, size_t len,
                                        gmx_route_seg_header_t *out)
{
    if (!data || !out || len < GMX_ROUTE_SEG_HEADER_SIZE)
        return GMX_ERR_TRUNCATED;

    out->next_hop_addr = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    out->next_hop_port = (uint16_t)data[4] | ((uint16_t)data[5] << 8);
    out->metric        = data[6];
    out->flags         = data[7];
    out->checksum      = (uint32_t)data[8] | ((uint32_t)data[9] << 8) |
                         ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);

    /* Validate segment checksum */
    uint32_t computed = gmx_crc32c(0, data, 8);
    if (computed != out->checksum) return GMX_ERR_CRC_MISMATCH;

    return GMX_OK;
}

size_t gmx_route_segment_encode(uint8_t *dst, size_t cap,
                                  uint32_t next_hop_addr,
                                  uint16_t next_hop_port,
                                  uint8_t metric, uint8_t flags)
{
    if (!dst || cap < GMX_ROUTE_SEG_HEADER_SIZE) return 0;

    dst[0] = (uint8_t)(next_hop_addr & 0xFF);
    dst[1] = (uint8_t)((next_hop_addr >> 8) & 0xFF);
    dst[2] = (uint8_t)((next_hop_addr >> 16) & 0xFF);
    dst[3] = (uint8_t)((next_hop_addr >> 24) & 0xFF);
    dst[4] = (uint8_t)(next_hop_port & 0xFF);
    dst[5] = (uint8_t)((next_hop_port >> 8) & 0xFF);
    dst[6] = metric;
    dst[7] = flags;

    uint32_t checksum = gmx_crc32c(0, dst, 8);
    dst[8]  = (uint8_t)(checksum & 0xFF);
    dst[9]  = (uint8_t)((checksum >> 8) & 0xFF);
    dst[10] = (uint8_t)((checksum >> 16) & 0xFF);
    dst[11] = (uint8_t)((checksum >> 24) & 0xFF);

    return GMX_ROUTE_SEG_HEADER_SIZE;
}

bool gmx_route_segment_is_local(const gmx_route_seg_header_t *hdr)
{
    if (!hdr) return false;
    return (hdr->flags & 0x01) != 0;
}

bool gmx_route_segment_is_gateway(const gmx_route_seg_header_t *hdr)
{
    if (!hdr) return false;
    return (hdr->flags & 0x02) != 0;
}

uint32_t gmx_route_segment_compute_cost(const gmx_route_seg_header_t *hdr)
{
    if (!hdr) return 0xFFFFFFFF;
    return (uint32_t)hdr->metric * 256 + (hdr->flags & 0xF0);
}
