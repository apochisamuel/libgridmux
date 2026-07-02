/*
 * libgridmux - Ordered stream delivery with checkpointing
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_STREAM_H
#define GMX_STREAM_H

#include "types.h"
#include "error.h"

#define GMX_STREAM_BUF_SIZE  2048
#define GMX_STREAM_MAX_SEGS  32

struct gmx_stream {
    uint32_t       stream_id;
    uint32_t       channel_id;
    gmx_channel_t *channel;
    uint32_t       sequence;
    uint32_t       bytes_written;
    uint32_t       segment_count;
    uint8_t       *write_buf;
    size_t         write_buf_cap;
    size_t         write_buf_len;
    bool           active;
    bool           finalized;
};

gmx_stream_t  *gmx_stream_create(uint32_t stream_id, uint32_t channel_id,
                                   gmx_channel_t *channel);
void           gmx_stream_destroy(gmx_stream_t *s);

gmx_status_t   gmx_stream_write(gmx_stream_t *s, const uint8_t *data,
                                  size_t len);

/*
 * Forward data through the stream's channel route.
 * Reads from the channel's route cursor to determine next-hop,
 * then copies forwarding metadata into dst.
 */
gmx_status_t   gmx_stream_forward(gmx_stream_t *s, uint8_t *dst,
                                    size_t dst_cap, size_t *out_len);

gmx_status_t   gmx_stream_finalize(gmx_stream_t *s);

#endif /* GMX_STREAM_H */
