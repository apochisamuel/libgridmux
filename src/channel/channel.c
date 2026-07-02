/*
 * libgridmux - Channel multiplexing
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/channel.h"
#include "gmx/route.h"
#include <stdlib.h>
#include <string.h>

gmx_channel_t *gmx_channel_create(uint32_t channel_id, uint32_t session_id,
                                    uint32_t owner_token)
{
    gmx_channel_t *ch = calloc(1, sizeof(*ch));
    if (!ch) return NULL;

    ch->channel_id      = channel_id;
    ch->session_id      = session_id;
    ch->owner_token     = owner_token;
    ch->credit_available = GMX_CHANNEL_CREDIT_DEFAULT;
    ch->credit_window   = GMX_CHANNEL_CREDIT_DEFAULT;
    ch->sequence_out    = 0;
    ch->sequence_in     = 0;
    ch->route_cursor    = NULL;
    ch->route_bind_epoch = 0;
    ch->route_bind_gen  = 0;
    ch->active          = true;
    ch->migrate_pending = false;

    return ch;
}

void gmx_channel_destroy(gmx_channel_t *ch)
{
    if (!ch) return;
    ch->active = false;
    ch->route_cursor = NULL;
    free(ch);
}

gmx_status_t gmx_channel_bind_route(gmx_channel_t *ch,
                                      gmx_route_entry_t *entry,
                                      uint32_t current_epoch)
{
    if (!ch || !entry) {
        return GMX_ERR_BAD_STATE;
    }
    if (!ch->active) {
        return GMX_ERR_NOT_ACTIVE;
    }
    if (!entry->active) {
        return GMX_ERR_BAD_ROUTE;
    }

    /*
     * Cache the route entry pointer for fast forwarding lookups.
     * Record the epoch and generation at bind time for staleness
     * detection (used by higher-level consistency checks).
     */
    ch->route_cursor     = entry;
    ch->route_bind_epoch = current_epoch;
    ch->route_bind_gen   = entry->generation;

    entry->subscriber_count++;

    return GMX_OK;
}

gmx_route_entry_t *gmx_channel_get_route_cursor(const gmx_channel_t *ch)
{
    if (!ch) return NULL;
    return ch->route_cursor;
}

gmx_status_t gmx_channel_consume_credit(gmx_channel_t *ch)
{
    if (!ch || !ch->active) return GMX_ERR_NOT_ACTIVE;
    if (ch->credit_available == 0) return GMX_ERR_OVERFLOW;
    ch->credit_available--;
    ch->sequence_out++;
    return GMX_OK;
}

gmx_status_t gmx_channel_grant_credit(gmx_channel_t *ch, uint32_t amount)
{
    if (!ch || !ch->active) return GMX_ERR_NOT_ACTIVE;
    if (ch->credit_available + amount < ch->credit_available) {
        return GMX_ERR_OVERFLOW;
    }
    ch->credit_available += amount;
    return GMX_OK;
}
