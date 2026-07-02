/*
 * libgridmux - Wire frame operations and next-hop resolution
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/wire.h"
#include "gmx/stream.h"
#include "gmx/channel.h"
#include "gmx/route.h"
#include <string.h>

gmx_status_t gmx_wire_resolve_next_hop(const gmx_stream_t *stream,
                                        uint8_t *dst, size_t dst_cap,
                                        size_t *out_len)
{
    if (!stream || !stream->channel || !dst) {
        return GMX_ERR_BAD_STATE;
    }

    gmx_route_entry_t *cursor = gmx_channel_get_route_cursor(stream->channel);
    if (!cursor) {
        return GMX_ERR_NOT_FOUND;
    }

    /* Build forwarding descriptor from route entry */
    size_t needed = sizeof(uint32_t) * 3 + cursor->segment_len;
    if (needed > dst_cap) {
        return GMX_ERR_OVERFLOW;
    }

    size_t off = 0;
    gmx_wire_write_u32(dst + off, cursor->next_hop_addr);  off += 4;
    gmx_wire_write_u32(dst + off, cursor->route_id);       off += 4;
    gmx_wire_write_u32(dst + off, cursor->segment_id);     off += 4;

    if (cursor->segment_data && cursor->segment_len > 0) {
        memcpy(dst + off, cursor->segment_data, cursor->segment_len);
        off += cursor->segment_len;
    }

    *out_len = off;
    return GMX_OK;
}
