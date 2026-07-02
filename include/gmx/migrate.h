/*
 * libgridmux - Live channel/stream migration between sessions
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_MIGRATE_H
#define GMX_MIGRATE_H

#include "types.h"
#include "error.h"

#define GMX_MIGRATE_MAX_CHANNELS  16
#define GMX_MIGRATE_MAX_LEASES    16

typedef enum gmx_migrate_state {
    GMX_MIGRATE_IDLE       = 0,
    GMX_MIGRATE_PREPARING  = 1,
    GMX_MIGRATE_TRANSFERRING = 2,
    GMX_MIGRATE_COMMITTING = 3,
    GMX_MIGRATE_DONE       = 4,
    GMX_MIGRATE_ROLLED_BACK = 5
} gmx_migrate_state_t;

typedef struct gmx_migrate_ctx {
    uint32_t              migration_id;
    uint32_t              source_session_id;
    uint32_t              dest_session_id;
    gmx_migrate_state_t   state;
    uint32_t              generation;
    uint32_t              channel_ids[GMX_MIGRATE_MAX_CHANNELS];
    uint32_t              channel_count;
    uint32_t              lease_ids[GMX_MIGRATE_MAX_LEASES];
    uint32_t              lease_count;
    uint32_t              credit_snapshot[GMX_MIGRATE_MAX_CHANNELS];
    uint32_t              pending_debt[GMX_MIGRATE_MAX_CHANNELS];
    bool                  channels_imported;
} gmx_migrate_ctx_t;

gmx_status_t gmx_migrate_ctx_init(gmx_migrate_ctx_t *ctx);
void         gmx_migrate_ctx_reset(gmx_migrate_ctx_t *ctx);

gmx_status_t gmx_migrate_prepare(gmx_migrate_ctx_t *ctx,
                                   uint32_t migration_id,
                                   uint32_t source_session,
                                   uint32_t dest_session);

gmx_status_t gmx_migrate_add_channel(gmx_migrate_ctx_t *ctx,
                                       uint32_t channel_id,
                                       uint32_t credit_available,
                                       uint32_t pending_debt);

gmx_status_t gmx_migrate_add_lease(gmx_migrate_ctx_t *ctx,
                                     uint32_t lease_id);

gmx_status_t gmx_migrate_transfer(gmx_migrate_ctx_t *ctx);

gmx_status_t gmx_migrate_commit(gmx_migrate_ctx_t *ctx);

gmx_status_t gmx_migrate_import_channels(gmx_migrate_ctx_t *ctx,
                                           void *dest_session);

gmx_status_t gmx_migrate_transfer_leases(gmx_migrate_ctx_t *ctx,
                                           void *source_lease_table,
                                           void *dest_lease_table);

gmx_status_t gmx_migrate_rollback(gmx_migrate_ctx_t *ctx);

gmx_status_t gmx_migrate_registry_entry(void *src_registry, void *dst_registry,
                                          uint32_t object_id, uint32_t type);

#endif /* GMX_MIGRATE_H */
