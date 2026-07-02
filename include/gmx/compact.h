/*
 * libgridmux - Deferred compaction and retention watermark management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_COMPACT_H
#define GMX_COMPACT_H

#include "types.h"
#include "error.h"

struct gmx_compact_ctx {
    uint32_t  watermark_epoch;
    uint32_t  sweep_generation;
    uint32_t  entries_freed;
    uint32_t  entries_scanned;
    bool      active;
};

gmx_status_t gmx_compact_ctx_init(gmx_compact_ctx_t *ctx);
void         gmx_compact_ctx_reset(gmx_compact_ctx_t *ctx);

/*
 * Compact the route table by freeing entries whose epoch_created
 * is older than (current_epoch - threshold).
 *
 * Note: does not currently invalidate cached route cursors held
 * by other subsystems.
 */
gmx_status_t gmx_compact_route_table(gmx_compact_ctx_t *ctx,
                                      gmx_route_table_t *table,
                                      uint32_t current_epoch,
                                      uint32_t threshold);

/* Process deferred journal events from the queue */
struct gmx_journal_deferred_queue;
typedef struct gmx_journal_deferred_queue gmx_journal_deferred_queue_t;

gmx_status_t gmx_compact_process_deferred(gmx_compact_ctx_t *ctx,
                                            gmx_journal_deferred_queue_t *queue,
                                            uint32_t current_epoch);

/* Compact retired segments past retention watermark */
gmx_status_t gmx_compact_retire_segments(gmx_compact_ctx_t *ctx,
                                          void *segment_pool,
                                          uint32_t current_epoch);

#endif /* GMX_COMPACT_H */
