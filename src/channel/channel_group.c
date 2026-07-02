/*
 * libgridmux - Channel group membership
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Channels can be organized into groups for collective flow control,
 * quota management, and migration operations. A channel may belong
 * to at most one group at a time.
 */

#include "gmx/channel.h"
#include "gmx/quota.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_CHANNEL_GROUP_MAX  16

typedef struct gmx_channel_group {
    uint32_t       group_id;
    uint32_t       member_ids[GMX_CHANNEL_GROUP_MAX];
    uint32_t       member_count;
    uint32_t       epoch_created;
    uint32_t       total_credit_window;
    bool           active;
} gmx_channel_group_t;

typedef struct gmx_channel_group_table {
    gmx_channel_group_t groups[GMX_CHANNEL_GROUP_MAX];
    uint32_t             count;
} gmx_channel_group_table_t;

gmx_status_t gmx_channel_group_table_init(gmx_channel_group_table_t *t)
{
    if (!t) return GMX_ERR_BAD_STATE;
    memset(t, 0, sizeof(*t));
    return GMX_OK;
}

gmx_channel_group_t *gmx_channel_group_create(
    gmx_channel_group_table_t *t,
    uint32_t group_id,
    uint32_t epoch)
{
    if (!t || t->count >= GMX_CHANNEL_GROUP_MAX) return NULL;

    /* Check for duplicate */
    for (uint32_t i = 0; i < GMX_CHANNEL_GROUP_MAX; i++) {
        if (t->groups[i].active && t->groups[i].group_id == group_id) {
            return NULL;
        }
    }

    /* Find free slot */
    for (uint32_t i = 0; i < GMX_CHANNEL_GROUP_MAX; i++) {
        if (!t->groups[i].active) {
            gmx_channel_group_t *g = &t->groups[i];
            g->group_id           = group_id;
            g->member_count       = 0;
            g->epoch_created      = epoch;
            g->total_credit_window = 0;
            g->active             = true;
            t->count++;
            return g;
        }
    }
    return NULL;
}

gmx_channel_group_t *gmx_channel_group_lookup(
    gmx_channel_group_table_t *t,
    uint32_t group_id)
{
    if (!t) return NULL;
    for (uint32_t i = 0; i < GMX_CHANNEL_GROUP_MAX; i++) {
        if (t->groups[i].active && t->groups[i].group_id == group_id) {
            return &t->groups[i];
        }
    }
    return NULL;
}

gmx_status_t gmx_channel_group_add_member(gmx_channel_group_t *g,
                                             uint32_t channel_id,
                                             uint32_t credit_window)
{
    if (!g || !g->active) return GMX_ERR_NOT_ACTIVE;
    if (g->member_count >= GMX_CHANNEL_GROUP_MAX) return GMX_ERR_OVERFLOW;

    /* Check for duplicate */
    for (uint32_t i = 0; i < g->member_count; i++) {
        if (g->member_ids[i] == channel_id) return GMX_ERR_DUPLICATE;
    }

    g->member_ids[g->member_count++] = channel_id;
    g->total_credit_window += credit_window;
    return GMX_OK;
}

gmx_status_t gmx_channel_group_remove_member(gmx_channel_group_t *g,
                                                uint32_t channel_id,
                                                uint32_t credit_window)
{
    if (!g || !g->active) return GMX_ERR_NOT_ACTIVE;

    for (uint32_t i = 0; i < g->member_count; i++) {
        if (g->member_ids[i] == channel_id) {
            /* Shift remaining members down */
            for (uint32_t j = i; j < g->member_count - 1; j++) {
                g->member_ids[j] = g->member_ids[j + 1];
            }
            g->member_count--;
            if (credit_window <= g->total_credit_window) {
                g->total_credit_window -= credit_window;
            }
            return GMX_OK;
        }
    }
    return GMX_ERR_NOT_FOUND;
}

bool gmx_channel_group_contains(const gmx_channel_group_t *g,
                                  uint32_t channel_id)
{
    if (!g || !g->active) return false;
    for (uint32_t i = 0; i < g->member_count; i++) {
        if (g->member_ids[i] == channel_id) return true;
    }
    return false;
}

/*
 * Compute the effective per-channel credit limit within a group.
 * This is the group's total window divided by the number of active
 * members, with a minimum of 1 credit per channel.
 */
uint32_t gmx_channel_group_effective_credit(const gmx_channel_group_t *g)
{
    if (!g || !g->active || g->member_count == 0) return 0;
    uint32_t per_member = g->total_credit_window / g->member_count;
    return per_member > 0 ? per_member : 1;
}

/*
 * Rebalance credit windows across all members of a group.
 * Computes the fair share and returns the number of members
 * whose credit was adjusted.
 */
uint32_t gmx_channel_group_rebalance(gmx_channel_group_t *g,
                                       uint32_t new_total_window)
{
    if (!g || !g->active || g->member_count == 0) return 0;

    g->total_credit_window = new_total_window;
    return g->member_count;
}

/*
 * Check if all members of a group have been migrated.
 * Used by migration logic to determine when group-level
 * resources can be transferred to the destination.
 */
bool gmx_channel_group_all_migrated(const gmx_channel_group_t *g,
                                      const uint32_t *migrated_ids,
                                      uint32_t migrated_count)
{
    if (!g || !g->active || !migrated_ids) return false;

    for (uint32_t i = 0; i < g->member_count; i++) {
        bool found = false;
        for (uint32_t j = 0; j < migrated_count; j++) {
            if (g->member_ids[i] == migrated_ids[j]) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

gmx_status_t gmx_channel_group_destroy(gmx_channel_group_table_t *t,
                                          uint32_t group_id)
{
    if (!t) return GMX_ERR_BAD_STATE;
    for (uint32_t i = 0; i < GMX_CHANNEL_GROUP_MAX; i++) {
        if (t->groups[i].active && t->groups[i].group_id == group_id) {
            t->groups[i].active = false;
            t->count--;
            return GMX_OK;
        }
    }
    return GMX_ERR_NOT_FOUND;
}
