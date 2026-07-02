/*
 * libgridmux - Basic session usage example
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Demonstrates programmatic construction of a valid GMX session
 * with route advertisement, channel creation, stream management,
 * and data forwarding.
 */
#include "gmx/session.h"
#include "gmx/channel.h"
#include "gmx/stream.h"
#include "gmx/route.h"
#include "gmx/wire.h"
#include "gmx/crc.h"
#include "gmx/types.h"
#include <stdio.h>
#include <string.h>

static void print_hex(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
        if ((i + 1) % 16 == 0) printf("\n    ");
        else if ((i + 1) % 4 == 0) printf(" ");
    }
    printf("\n");
}

int main(void)
{
    printf("libgridmux basic session example\n");
    printf("================================\n\n");

    /* Create a session */
    gmx_session_t *sess = gmx_session_create(1);
    if (!sess) {
        fprintf(stderr, "Failed to create session\n");
        return 1;
    }
    printf("Created session (id=%u)\n", sess->session_id);

    /* Activate session directly (bypassing wire protocol) */
    sess->active = true;
    sess->owner_token = 0xCAFEBABE;
    printf("Session activated (epoch=%u)\n", sess->epoch);

    /* Create a route entry */
    uint8_t route_seg[] = {10, 0, 0, 1, 80, 0, 0xAA, 0xBB};
    gmx_route_entry_t *route = gmx_route_entry_create(
        sess->route_table, 1, 1, sess->epoch, route_seg, sizeof(route_seg));
    if (!route) {
        fprintf(stderr, "Failed to create route\n");
        gmx_session_destroy(sess);
        return 1;
    }
    printf("Created route entry (id=%u, next_hop=%u.%u.%u.%u:%u)\n",
           route->route_id,
           route_seg[0], route_seg[1], route_seg[2], route_seg[3],
           route_seg[4] | (route_seg[5] << 8));

    /* Create a channel */
    gmx_channel_t *ch = gmx_channel_create(1, sess->session_id, sess->owner_token);
    if (!ch) {
        fprintf(stderr, "Failed to create channel\n");
        gmx_session_destroy(sess);
        return 1;
    }
    sess->channels[0] = ch;
    sess->channel_count = 1;
    printf("Created channel (id=%u, credit=%u)\n",
           ch->channel_id, ch->credit_available);

    /* Bind channel to route */
    gmx_status_t rc = gmx_channel_bind_route(ch, route, sess->epoch);
    printf("Bound channel to route (rc=%d)\n", rc);

    /* Create a stream on the channel */
    gmx_stream_t *s = gmx_stream_create(1, ch->channel_id, ch);
    if (!s) {
        fprintf(stderr, "Failed to create stream\n");
        gmx_session_destroy(sess);
        return 1;
    }
    sess->streams[0] = s;
    sess->stream_count = 1;
    printf("Created stream (id=%u)\n", s->stream_id);

    /* Write data to stream */
    uint8_t payload[] = "Hello, GridMux!";
    rc = gmx_stream_write(s, payload, sizeof(payload));
    printf("Wrote %zu bytes to stream (rc=%d, total=%u)\n",
           sizeof(payload), rc, s->bytes_written);

    /* Forward data through stream's route */
    uint8_t fwd_buf[256];
    size_t fwd_len = 0;
    rc = gmx_stream_forward(s, fwd_buf, sizeof(fwd_buf), &fwd_len);
    printf("Forwarded data (rc=%d, len=%zu)\n", rc, fwd_len);
    if (rc == GMX_OK && fwd_len > 0) {
        printf("  Forwarding descriptor:\n    ");
        print_hex(fwd_buf, fwd_len);
    }

    /* Clean up */
    gmx_session_destroy(sess);
    printf("\nSession destroyed. Done.\n");

    return 0;
}
