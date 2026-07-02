/*
 * libgridmux - Channel lifecycle state machine
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Defines the channel state machine: creation, active, draining,
 * migrating, closing, and closed states. Enforces valid transitions
 * and provides lifecycle event hooks.
 */
#include "gmx/channel.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

typedef enum gmx_channel_state {
    GMX_CH_STATE_CREATED   = 0,
    GMX_CH_STATE_ACTIVE    = 1,
    GMX_CH_STATE_DRAINING  = 2,
    GMX_CH_STATE_MIGRATING = 3,
    GMX_CH_STATE_CLOSING   = 4,
    GMX_CH_STATE_CLOSED    = 5
} gmx_channel_state_t;

typedef struct gmx_channel_lifecycle {
    uint32_t           channel_id;
    gmx_channel_state_t state;
    uint32_t           epoch_created;
    uint32_t           epoch_activated;
    uint32_t           epoch_closed;
    uint32_t           total_writes;
    uint32_t           total_reads;
    uint32_t           total_credits_consumed;
    uint32_t           transition_count;
} gmx_channel_lifecycle_t;

gmx_status_t gmx_channel_lifecycle_init(gmx_channel_lifecycle_t *lc,
                                           uint32_t channel_id,
                                           uint32_t epoch)
{
    if (!lc) return GMX_ERR_BAD_STATE;
    memset(lc, 0, sizeof(*lc));
    lc->channel_id = channel_id;
    lc->state = GMX_CH_STATE_CREATED;
    lc->epoch_created = epoch;
    return GMX_OK;
}

static bool is_valid_transition(gmx_channel_state_t from,
                                  gmx_channel_state_t to)
{
    switch (from) {
    case GMX_CH_STATE_CREATED:
        return to == GMX_CH_STATE_ACTIVE || to == GMX_CH_STATE_CLOSED;
    case GMX_CH_STATE_ACTIVE:
        return to == GMX_CH_STATE_DRAINING ||
               to == GMX_CH_STATE_MIGRATING ||
               to == GMX_CH_STATE_CLOSING;
    case GMX_CH_STATE_DRAINING:
        return to == GMX_CH_STATE_CLOSING;
    case GMX_CH_STATE_MIGRATING:
        return to == GMX_CH_STATE_ACTIVE ||  /* migration rollback */
               to == GMX_CH_STATE_CLOSED;    /* migration complete */
    case GMX_CH_STATE_CLOSING:
        return to == GMX_CH_STATE_CLOSED;
    case GMX_CH_STATE_CLOSED:
        return false;
    }
    return false;
}

gmx_status_t gmx_channel_lifecycle_transition(gmx_channel_lifecycle_t *lc,
                                                 gmx_channel_state_t new_state,
                                                 uint32_t epoch)
{
    if (!lc) return GMX_ERR_BAD_STATE;
    if (!is_valid_transition(lc->state, new_state)) return GMX_ERR_BAD_STATE;

    lc->state = new_state;
    lc->transition_count++;

    switch (new_state) {
    case GMX_CH_STATE_ACTIVE:
        lc->epoch_activated = epoch;
        break;
    case GMX_CH_STATE_CLOSED:
        lc->epoch_closed = epoch;
        break;
    default:
        break;
    }

    return GMX_OK;
}

gmx_status_t gmx_channel_lifecycle_record_write(gmx_channel_lifecycle_t *lc)
{
    if (!lc || lc->state != GMX_CH_STATE_ACTIVE) return GMX_ERR_BAD_STATE;
    lc->total_writes++;
    return GMX_OK;
}

gmx_status_t gmx_channel_lifecycle_record_read(gmx_channel_lifecycle_t *lc)
{
    if (!lc) return GMX_ERR_BAD_STATE;
    if (lc->state != GMX_CH_STATE_ACTIVE &&
        lc->state != GMX_CH_STATE_DRAINING) return GMX_ERR_BAD_STATE;
    lc->total_reads++;
    return GMX_OK;
}

gmx_status_t gmx_channel_lifecycle_record_credit(gmx_channel_lifecycle_t *lc,
                                                     uint32_t amount)
{
    if (!lc) return GMX_ERR_BAD_STATE;
    lc->total_credits_consumed += amount;
    return GMX_OK;
}

bool gmx_channel_lifecycle_is_active(const gmx_channel_lifecycle_t *lc)
{
    return lc && lc->state == GMX_CH_STATE_ACTIVE;
}

bool gmx_channel_lifecycle_is_closeable(const gmx_channel_lifecycle_t *lc)
{
    return lc && (lc->state == GMX_CH_STATE_DRAINING ||
                  lc->state == GMX_CH_STATE_CLOSING);
}

uint32_t gmx_channel_lifecycle_age(const gmx_channel_lifecycle_t *lc,
                                     uint32_t current_epoch)
{
    if (!lc || current_epoch < lc->epoch_created) return 0;
    return current_epoch - lc->epoch_created;
}

const char *gmx_channel_state_name(gmx_channel_state_t state)
{
    switch (state) {
    case GMX_CH_STATE_CREATED:   return "created";
    case GMX_CH_STATE_ACTIVE:    return "active";
    case GMX_CH_STATE_DRAINING:  return "draining";
    case GMX_CH_STATE_MIGRATING: return "migrating";
    case GMX_CH_STATE_CLOSING:   return "closing";
    case GMX_CH_STATE_CLOSED:    return "closed";
    }
    return "unknown";
}
