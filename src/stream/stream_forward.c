/*
 * libgridmux - Stream forwarding descriptor construction
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Builds forwarding descriptors that encode the routing path
 * and payload for data flowing through the mesh. Handles multi-hop
 * forwarding, metric aggregation, and path MTU negotiation.
 */
#include "gmx/stream.h"
#include "gmx/route.h"
#include "gmx/wire.h"
#include "gmx/crc.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_FWD_DESC_HEADER    16
#define GMX_FWD_MAX_HOPS       8
#define GMX_FWD_DEFAULT_MTU    1500

typedef struct gmx_fwd_hop {
    uint32_t next_hop_addr;
    uint16_t next_hop_port;
    uint8_t  metric;
    uint8_t  ttl;
} gmx_fwd_hop_t;

typedef struct gmx_fwd_descriptor {
    uint32_t       stream_id;
    uint32_t       route_id;
    uint32_t       sequence;
    uint32_t       total_length;
    gmx_fwd_hop_t hops[GMX_FWD_MAX_HOPS];
    uint32_t       hop_count;
    uint32_t       mtu;
    uint32_t       checksum;
} gmx_fwd_descriptor_t;

gmx_status_t gmx_fwd_descriptor_init(gmx_fwd_descriptor_t *desc,
                                        uint32_t stream_id,
                                        uint32_t route_id,
                                        uint32_t sequence)
{
    if (!desc) return GMX_ERR_BAD_STATE;
    memset(desc, 0, sizeof(*desc));
    desc->stream_id = stream_id;
    desc->route_id = route_id;
    desc->sequence = sequence;
    desc->mtu = GMX_FWD_DEFAULT_MTU;
    return GMX_OK;
}

gmx_status_t gmx_fwd_descriptor_add_hop(gmx_fwd_descriptor_t *desc,
                                           uint32_t next_hop_addr,
                                           uint16_t next_hop_port,
                                           uint8_t metric, uint8_t ttl)
{
    if (!desc || desc->hop_count >= GMX_FWD_MAX_HOPS)
        return GMX_ERR_OVERFLOW;

    gmx_fwd_hop_t *hop = &desc->hops[desc->hop_count];
    hop->next_hop_addr = next_hop_addr;
    hop->next_hop_port = next_hop_port;
    hop->metric = metric;
    hop->ttl = ttl;

    desc->hop_count++;
    return GMX_OK;
}

size_t gmx_fwd_descriptor_serialize(const gmx_fwd_descriptor_t *desc,
                                      uint8_t *dst, size_t dst_cap)
{
    if (!desc || !dst) return 0;

    size_t needed = GMX_FWD_DESC_HEADER + desc->hop_count * 8;
    if (dst_cap < needed) return 0;

    size_t off = 0;
    gmx_wire_write_u32(dst + off, desc->stream_id);    off += 4;
    gmx_wire_write_u32(dst + off, desc->route_id);     off += 4;
    gmx_wire_write_u32(dst + off, desc->sequence);     off += 4;
    gmx_wire_write_u32(dst + off, desc->hop_count);    off += 4;

    for (uint32_t i = 0; i < desc->hop_count; i++) {
        const gmx_fwd_hop_t *h = &desc->hops[i];
        gmx_wire_write_u32(dst + off, h->next_hop_addr);   off += 4;
        gmx_wire_write_u16(dst + off, h->next_hop_port);   off += 2;
        dst[off++] = h->metric;
        dst[off++] = h->ttl;
    }

    return off;
}

gmx_status_t gmx_fwd_descriptor_deserialize(gmx_fwd_descriptor_t *desc,
                                               const uint8_t *src,
                                               size_t src_len)
{
    if (!desc || !src || src_len < GMX_FWD_DESC_HEADER)
        return GMX_ERR_TRUNCATED;

    memset(desc, 0, sizeof(*desc));

    size_t off = 0;
    desc->stream_id  = gmx_wire_read_u32(src + off);   off += 4;
    desc->route_id   = gmx_wire_read_u32(src + off);   off += 4;
    desc->sequence   = gmx_wire_read_u32(src + off);   off += 4;
    desc->hop_count  = gmx_wire_read_u32(src + off);   off += 4;

    if (desc->hop_count > GMX_FWD_MAX_HOPS) return GMX_ERR_OVERFLOW;
    if (src_len < GMX_FWD_DESC_HEADER + desc->hop_count * 8)
        return GMX_ERR_TRUNCATED;

    for (uint32_t i = 0; i < desc->hop_count; i++) {
        desc->hops[i].next_hop_addr = gmx_wire_read_u32(src + off); off += 4;
        desc->hops[i].next_hop_port = gmx_wire_read_u16(src + off); off += 2;
        desc->hops[i].metric = src[off++];
        desc->hops[i].ttl    = src[off++];
    }

    desc->mtu = GMX_FWD_DEFAULT_MTU;
    return GMX_OK;
}

uint32_t gmx_fwd_total_metric(const gmx_fwd_descriptor_t *desc)
{
    if (!desc) return 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < desc->hop_count; i++) {
        total += desc->hops[i].metric;
    }
    return total;
}

uint8_t gmx_fwd_min_ttl(const gmx_fwd_descriptor_t *desc)
{
    if (!desc || desc->hop_count == 0) return 0;
    uint8_t min = 255;
    for (uint32_t i = 0; i < desc->hop_count; i++) {
        if (desc->hops[i].ttl < min) min = desc->hops[i].ttl;
    }
    return min;
}

bool gmx_fwd_is_direct(const gmx_fwd_descriptor_t *desc)
{
    return desc && desc->hop_count <= 1;
}

gmx_status_t gmx_fwd_negotiate_mtu(gmx_fwd_descriptor_t *desc,
                                      uint32_t peer_mtu)
{
    if (!desc) return GMX_ERR_BAD_STATE;
    if (peer_mtu < desc->mtu) desc->mtu = peer_mtu;
    /* Reserve space for forwarding header */
    if (desc->mtu > GMX_FWD_DESC_HEADER) {
        desc->mtu -= GMX_FWD_DESC_HEADER;
    }
    return GMX_OK;
}

uint32_t gmx_fwd_descriptor_checksum(const gmx_fwd_descriptor_t *desc)
{
    if (!desc) return 0;
    uint8_t buf[256];
    size_t len = gmx_fwd_descriptor_serialize(desc, buf, sizeof(buf));
    if (len == 0) return 0;
    return gmx_crc32c(0, buf, len);
}
