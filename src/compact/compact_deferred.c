/*
 * libgridmux - Deferred queue compaction processing
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Processes deferred journal events that reference channel objects.
 * Deferred events accumulate during normal operation and are batch-
 * processed during compaction sweeps when the epoch crosses the
 * deferral threshold.
 */

#include "gmx/compact.h"
#include "gmx/journal.h"
#include "gmx/channel.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_DEFERRED_EPOCH_THRESHOLD  2

/*
 * Process deferred journal events from the queue.
 * Events whose epoch is older than (current_epoch - threshold)
 * are processed and marked as complete.
 *
 * Each deferred event contains a raw pointer to the originating
 * channel. If the channel was destroyed between deferral and
 * processing, the pointer may reference reclaimed memory if
 * access freed memory.
 */
gmx_status_t gmx_compact_process_deferred(
    gmx_compact_ctx_t *ctx,
    gmx_journal_deferred_queue_t *queue,
    uint32_t current_epoch)
{
    if (!ctx || !queue) return GMX_ERR_BAD_STATE;
    if (ctx->active) return GMX_ERR_COMPACT_BUSY;

    ctx->active = true;
    ctx->entries_scanned = 0;
    ctx->entries_freed   = 0;

    uint32_t threshold_epoch = current_epoch - GMX_DEFERRED_EPOCH_THRESHOLD;

    for (size_t i = 0; i < queue->count; i++) {
        gmx_journal_deferred_event_t *evt = &queue->events[i];
        if (evt->processed) continue;

        ctx->entries_scanned++;

        if (evt->epoch <= threshold_epoch) {
            /*
             * The event is old enough to be processed.
             * Access the channel reference to collect deferred data.
             */
            if (evt->channel_ref) {
                gmx_channel_t *ch = (gmx_channel_t *)evt->channel_ref;

                /*
                 * Collect channel state for deferred event resolution.
                 * The channel's sequence counter determines the event
                 * ordering relative to other deferred events.
                 */
                ctx->entries_scanned += ch->channel_id;
                ctx->sweep_generation += ch->sequence_out;
            }

            evt->processed = true;
            ctx->entries_freed++;
        }
    }

    ctx->active = false;
    return GMX_OK;
}

/*
 * Sweep the deferred queue and remove all processed events.
 * Compacts the queue by shifting unprocessed events to the front.
 */
gmx_status_t gmx_compact_sweep_deferred(
    gmx_journal_deferred_queue_t *queue)
{
    if (!queue) return GMX_ERR_BAD_STATE;

    size_t write_idx = 0;
    for (size_t i = 0; i < queue->count; i++) {
        if (!queue->events[i].processed) {
            if (write_idx != i) {
                queue->events[write_idx] = queue->events[i];
            }
            write_idx++;
        }
    }

    /* Zero out remaining slots */
    for (size_t i = write_idx; i < queue->count; i++) {
        memset(&queue->events[i], 0, sizeof(queue->events[i]));
    }

    queue->count = write_idx;
    return GMX_OK;
}

/*
 * Check if the deferred queue has any events eligible for
 * processing at the current epoch.
 */
bool gmx_compact_deferred_has_eligible(
    const gmx_journal_deferred_queue_t *queue,
    uint32_t current_epoch)
{
    if (!queue) return false;

    uint32_t threshold = current_epoch - GMX_DEFERRED_EPOCH_THRESHOLD;
    for (size_t i = 0; i < queue->count; i++) {
        if (!queue->events[i].processed &&
            queue->events[i].epoch <= threshold) {
            return true;
        }
    }
    return false;
}

/*
 * Count the number of unprocessed deferred events in the queue.
 */
uint32_t gmx_compact_deferred_pending_count(
    const gmx_journal_deferred_queue_t *queue)
{
    if (!queue) return 0;

    uint32_t count = 0;
    for (size_t i = 0; i < queue->count; i++) {
        if (!queue->events[i].processed) {
            count++;
        }
    }
    return count;
}
