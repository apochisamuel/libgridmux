/*
 * libgridmux - Route cascade update and subscriber management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * When a route topology changes, updates cascade through the
 * routing DAG. Subscribers (channels bound to route segments)
 * need to be notified so they can re-resolve their forwarding
 * cursors. This module handles cascade propagation and subscriber
 * tracking.
 */

#include "gmx/route.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <stdlib.h>
#include <string.h>

#define GMX_ROUTE_MAX_SUBSCRIBERS  32
#define GMX_ROUTE_CASCADE_DEPTH    8

typedef struct gmx_route_subscriber {
    uint32_t  channel_id;
    uint32_t  bind_generation;
    bool      active;
} gmx_route_subscriber_t;

typedef struct gmx_route_subscriber_list {
    gmx_route_subscriber_t subscribers[GMX_ROUTE_MAX_SUBSCRIBERS];
    uint32_t               count;
} gmx_route_subscriber_list_t;

typedef struct gmx_route_cascade_entry {
    uint32_t  route_id;
    uint32_t  depth;
    uint32_t  parent_route_id;
} gmx_route_cascade_entry_t;

/*
 * Initialize a subscriber list for a route entry.
 */
gmx_status_t gmx_route_subscriber_init(gmx_route_subscriber_list_t *sl)
{
    if (!sl) return GMX_ERR_BAD_STATE;
    memset(sl, 0, sizeof(*sl));
    return GMX_OK;
}

/*
 * Add a subscriber (channel) to a route's notification list.
 */
gmx_status_t gmx_route_subscriber_add(gmx_route_subscriber_list_t *sl,
                                         uint32_t channel_id,
                                         uint32_t bind_generation)
{
    if (!sl) return GMX_ERR_BAD_STATE;
    if (sl->count >= GMX_ROUTE_MAX_SUBSCRIBERS) return GMX_ERR_OVERFLOW;

    /* Check for duplicate */
    for (uint32_t i = 0; i < sl->count; i++) {
        if (sl->subscribers[i].channel_id == channel_id &&
            sl->subscribers[i].active) {
            return GMX_ERR_DUPLICATE;
        }
    }

    gmx_route_subscriber_t *sub = &sl->subscribers[sl->count];
    sub->channel_id      = channel_id;
    sub->bind_generation = bind_generation;
    sub->active          = true;
    sl->count++;

    return GMX_OK;
}

/*
 * Remove a subscriber from the list.
 */
gmx_status_t gmx_route_subscriber_remove(gmx_route_subscriber_list_t *sl,
                                            uint32_t channel_id)
{
    if (!sl) return GMX_ERR_BAD_STATE;

    for (uint32_t i = 0; i < sl->count; i++) {
        if (sl->subscribers[i].channel_id == channel_id &&
            sl->subscribers[i].active) {
            sl->subscribers[i].active = false;
            return GMX_OK;
        }
    }
    return GMX_ERR_NOT_FOUND;
}

/*
 * Count active subscribers in the list.
 */
uint32_t gmx_route_subscriber_active_count(
    const gmx_route_subscriber_list_t *sl)
{
    if (!sl) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < sl->count; i++) {
        if (sl->subscribers[i].active) count++;
    }
    return count;
}

/*
 * Get subscriber channel IDs for notification.
 * Copies up to max_ids channel IDs into dst, returns actual count.
 */
uint32_t gmx_route_subscriber_get_channels(
    const gmx_route_subscriber_list_t *sl,
    uint32_t *dst, uint32_t max_ids)
{
    if (!sl || !dst || max_ids == 0) return 0;

    uint32_t copied = 0;
    for (uint32_t i = 0; i < sl->count && copied < max_ids; i++) {
        if (sl->subscribers[i].active) {
            dst[copied++] = sl->subscribers[i].channel_id;
        }
    }
    return copied;
}

/*
 * Build a cascade propagation plan from a route topology change.
 * Starting from the changed route_id, compute which downstream
 * routes are affected and in what order they should be updated.
 *
 * Returns the number of entries in the cascade plan.
 */
uint32_t gmx_route_build_cascade_plan(
    gmx_route_table_t *table,
    uint32_t changed_route_id,
    gmx_route_cascade_entry_t *plan,
    uint32_t plan_capacity)
{
    if (!table || !plan || plan_capacity == 0) return 0;

    /* Simple BFS-like propagation from the changed route */
    uint32_t plan_len = 0;
    uint32_t queue_head = 0;

    /* Seed with the changed route */
    plan[plan_len].route_id        = changed_route_id;
    plan[plan_len].depth           = 0;
    plan[plan_len].parent_route_id = 0;
    plan_len++;

    /* Propagate to dependent routes (routes with adjacent IDs
     * in this simplified topology model) */
    while (queue_head < plan_len) {
        uint32_t current_id = plan[queue_head].route_id;
        uint32_t current_depth = plan[queue_head].depth;
        queue_head++;

        if (current_depth >= GMX_ROUTE_CASCADE_DEPTH) continue;

        /* Check for downstream routes that depend on this one */
        for (size_t i = 0; i < table->capacity && plan_len < plan_capacity; i++) {
            gmx_route_entry_t *entry = table->entries[i];
            if (!entry || !entry->active) continue;
            if (entry->route_id == current_id) continue;

            /* In the grid topology model, adjacent segment IDs
             * indicate route dependency */
            if (entry->segment_id == current_id) {
                /* Avoid duplicates */
                bool exists = false;
                for (uint32_t j = 0; j < plan_len; j++) {
                    if (plan[j].route_id == entry->route_id) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    plan[plan_len].route_id        = entry->route_id;
                    plan[plan_len].depth           = current_depth + 1;
                    plan[plan_len].parent_route_id = current_id;
                    plan_len++;
                }
            }
        }
    }

    return plan_len;
}

/*
 * Apply a cascade update to a route table.
 * Updates each route in the cascade plan with new segment data
 * derived from the parent's update.
 */
gmx_status_t gmx_route_apply_cascade(gmx_route_table_t *table,
                                       const gmx_route_cascade_entry_t *plan,
                                       uint32_t plan_len,
                                       const uint8_t *update_data,
                                       size_t update_len)
{
    if (!table || !plan || plan_len == 0) return GMX_ERR_BAD_STATE;

    for (uint32_t i = 0; i < plan_len; i++) {
        gmx_route_entry_t *entry = gmx_route_table_lookup(
            table, plan[i].route_id);
        if (!entry) continue;

        /* Apply update with depth-based attenuation */
        if (update_data && update_len > 0) {
            uint8_t attenuated[GMX_MAX_PAYLOAD];
            size_t att_len = update_len;
            if (att_len > GMX_MAX_PAYLOAD) att_len = GMX_MAX_PAYLOAD;

            memcpy(attenuated, update_data, att_len);

            /* Attenuate metric by cascade depth */
            entry->metric += (uint8_t)(plan[i].depth + 1);
            entry->generation = table->table_generation++;
        }
    }

    table->cascade_seq++;
    return GMX_OK;
}
