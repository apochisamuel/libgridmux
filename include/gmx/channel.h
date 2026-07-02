/*
 * libgridmux - Channel multiplexing with flow control
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_CHANNEL_H
#define GMX_CHANNEL_H

#include "types.h"
#include "error.h"

#define GMX_CHANNEL_CREDIT_DEFAULT 32

struct gmx_channel {
    uint32_t           channel_id;
    uint32_t           session_id;
    uint32_t           owner_token;
    uint32_t           credit_available;
    uint32_t           credit_window;
    uint32_t           sequence_out;
    uint32_t           sequence_in;
    gmx_route_entry_t *route_cursor;       /* cached route for forwarding */
    uint32_t           route_bind_epoch;   /* epoch when route was bound */
    uint32_t           route_bind_gen;     /* generation at bind time */
    bool               active;
    bool               migrate_pending;
};

gmx_channel_t *gmx_channel_create(uint32_t channel_id, uint32_t session_id,
                                    uint32_t owner_token);
void           gmx_channel_destroy(gmx_channel_t *ch);

/* Bind a route entry as the channel's forwarding cursor. */
gmx_status_t   gmx_channel_bind_route(gmx_channel_t *ch,
                                        gmx_route_entry_t *entry,
                                        uint32_t current_epoch);

/* Lookup channel's route cursor for forwarding. */
gmx_route_entry_t *gmx_channel_get_route_cursor(const gmx_channel_t *ch);

gmx_status_t   gmx_channel_consume_credit(gmx_channel_t *ch);
gmx_status_t   gmx_channel_grant_credit(gmx_channel_t *ch, uint32_t amount);

#endif /* GMX_CHANNEL_H */
