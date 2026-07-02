/*
 * libgridmux - Session lifecycle management
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_SESSION_H
#define GMX_SESSION_H

#include "types.h"
#include "error.h"

struct gmx_session {
    uint32_t           session_id;
    uint32_t           epoch;
    uint32_t           expected_sequence;
    uint32_t           last_epoch_advance_seq;
    uint32_t           owner_token;
    gmx_registry_t    *registry;
    gmx_route_table_t *route_table;
    gmx_compact_ctx_t *compact_ctx;
    gmx_channel_t     *channels[GMX_MAX_CHANNELS];
    gmx_stream_t      *streams[GMX_MAX_STREAMS];
    size_t             channel_count;
    size_t             stream_count;
    bool               active;
    bool               epoch_advanced;

    /* Quota management */
    void              *quota_table;     /* gmx_quota_table_t */

    /* Lease management */
    void              *lease_table;     /* gmx_lease_table_t */

    /* Journal */
    void              *journal;         /* gmx_journal_t */
    void              *deferred_queue;  /* gmx_journal_deferred_queue_t */

    /* Crypto/nonce */
    void              *crypto_ctx;      /* gmx_crypto_ctx_t */

    /* Migration */
    void              *migrate_ctx;     /* gmx_migrate_ctx_t */

    /* Channel quota pointers (per-channel) */
    void              *channel_quota[GMX_MAX_CHANNELS];

    /* Stream lease pointers (per-stream) */
    void              *stream_lease[GMX_MAX_STREAMS];

    /* Checkpoint ring for replay scanning */
    uint32_t          *checkpoint_ring;
    uint32_t           checkpoint_ring_cap;
    uint32_t           checkpoint_ring_count;
};

/* Initialize a new session with the given session_id */
gmx_session_t *gmx_session_create(uint32_t session_id);

/* Destroy session and all owned resources */
void gmx_session_destroy(gmx_session_t *sess);

/* Ingest a complete set of wire-format frames for this session */
gmx_status_t gmx_session_ingest(gmx_session_t *sess,
                                 const uint8_t *data, size_t len);

/* Advance epoch counter; may trigger compaction eligibility */
gmx_status_t gmx_session_advance_epoch(gmx_session_t *sess, uint32_t new_epoch);

/* Process a single parsed frame */
gmx_status_t gmx_session_dispatch_frame(gmx_session_t *sess,
                                          const gmx_frame_t *frame);

/* Lookup channel by ID within this session */
gmx_channel_t *gmx_session_find_channel(gmx_session_t *sess,
                                          uint32_t channel_id);

/* Lookup stream by ID within this session */
gmx_stream_t *gmx_session_find_stream(gmx_session_t *sess,
                                        uint32_t stream_id);

#endif /* GMX_SESSION_H */
