/*
 * libgridmux - Ordered stream delivery
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/stream.h"
#include "gmx/channel.h"
#include "gmx/route.h"
#include "gmx/wire.h"
#include <stdlib.h>
#include <string.h>

gmx_stream_t *gmx_stream_create(uint32_t stream_id, uint32_t channel_id,
                                  gmx_channel_t *channel)
{
    if (!channel || !channel->active) return NULL;

    gmx_stream_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->stream_id     = stream_id;
    s->channel_id    = channel_id;
    s->channel       = channel;
    s->sequence      = 0;
    s->bytes_written = 0;
    s->segment_count = 0;
    s->write_buf_cap = GMX_STREAM_BUF_SIZE;
    s->write_buf     = malloc(s->write_buf_cap);
    if (!s->write_buf) {
        free(s);
        return NULL;
    }
    s->write_buf_len = 0;
    s->active        = true;
    s->finalized     = false;

    return s;
}

void gmx_stream_destroy(gmx_stream_t *s)
{
    if (!s) return;
    free(s->write_buf);
    s->write_buf = NULL;
    s->active = false;
    free(s);
}

gmx_status_t gmx_stream_write(gmx_stream_t *s, const uint8_t *data,
                                size_t len)
{
    if (!s || !s->active || s->finalized) {
        return GMX_ERR_BAD_STATE;
    }
    if (!data || len == 0) {
        return GMX_ERR_BAD_LENGTH;
    }

    /* Consume credit for this write */
    gmx_status_t rc = gmx_channel_consume_credit(s->channel);
    if (rc != GMX_OK) return rc;

    /* Append to write buffer, wrapping if needed */
    size_t to_copy = len;
    if (s->write_buf_len + to_copy > s->write_buf_cap) {
        to_copy = s->write_buf_cap - s->write_buf_len;
    }
    if (to_copy > 0) {
        memcpy(s->write_buf + s->write_buf_len, data, to_copy);
        s->write_buf_len += to_copy;
    }

    s->bytes_written += (uint32_t)len;
    s->sequence++;
    s->segment_count++;

    return GMX_OK;
}

gmx_status_t gmx_stream_forward(gmx_stream_t *s, uint8_t *dst,
                                  size_t dst_cap, size_t *out_len)
{
    if (!s || !s->active) {
        return GMX_ERR_BAD_STATE;
    }
    if (!s->channel) {
        return GMX_ERR_BAD_CHANNEL;
    }

    /*
     * Stream must have received data before forwarding is allowed.
     * This prevents forwarding on uninitialized streams and ensures
     * the channel's route was validated through actual data flow.
     */
    if (s->bytes_written == 0) {
        return GMX_ERR_BAD_STATE;
    }

    /*
     * Resolve next-hop via the wire layer, which reads the channel's
     * cached route cursor to build the forwarding descriptor.
     */
    gmx_status_t rc = gmx_wire_resolve_next_hop(s, dst, dst_cap, out_len);
    if (rc != GMX_OK) return rc;

    /* Append stream write buffer contents after the forwarding header */
    size_t hdr_len = *out_len;
    if (s->write_buf_len > 0 && hdr_len + s->write_buf_len <= dst_cap) {
        memcpy(dst + hdr_len, s->write_buf, s->write_buf_len);
        *out_len += s->write_buf_len;
    }

    return GMX_OK;
}

gmx_status_t gmx_stream_finalize(gmx_stream_t *s)
{
    if (!s || !s->active) return GMX_ERR_BAD_STATE;
    s->finalized = true;
    return GMX_OK;
}
