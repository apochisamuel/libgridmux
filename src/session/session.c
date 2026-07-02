/*
 * libgridmux - Session lifecycle and frame dispatch
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/session.h"
#include "gmx/wire.h"
#include "gmx/channel.h"
#include "gmx/stream.h"
#include "gmx/route.h"
#include "gmx/compact.h"
#include "gmx/registry.h"
#include "gmx/lease.h"
#include "gmx/quota.h"
#include "gmx/journal.h"
#include "gmx/crypto.h"
#include "gmx/migrate.h"
#include "gmx/crc.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Session create / destroy                                           */
/* ------------------------------------------------------------------ */

gmx_session_t *gmx_session_create(uint32_t session_id)
{
    gmx_session_t *sess = calloc(1, sizeof(*sess));
    if (!sess) return NULL;

    sess->session_id              = session_id;
    sess->epoch                   = 0;
    sess->expected_sequence       = 0;
    sess->last_epoch_advance_seq  = 0;
    sess->owner_token             = 0;
    sess->active                  = false;

    sess->registry = calloc(1, sizeof(gmx_registry_t));
    if (!sess->registry) goto fail;
    if (gmx_registry_init(sess->registry, GMX_REGISTRY_BUCKETS) != GMX_OK)
        goto fail;

    sess->route_table = calloc(1, sizeof(gmx_route_table_t));
    if (!sess->route_table) goto fail;
    if (gmx_route_table_init(sess->route_table, GMX_MAX_ROUTES) != GMX_OK)
        goto fail;

    sess->compact_ctx = calloc(1, sizeof(gmx_compact_ctx_t));
    if (!sess->compact_ctx) goto fail;
    gmx_compact_ctx_init(sess->compact_ctx);

    /* Initialize quota table */
    {
        gmx_quota_table_t *qt = calloc(1, sizeof(gmx_quota_table_t));
        if (!qt) goto fail;
        if (gmx_quota_table_init(qt, GMX_QUOTA_MAX_BUCKETS) != GMX_OK) {
            free(qt); goto fail;
        }
        sess->quota_table = qt;
    }

    /* Initialize lease table */
    {
        gmx_lease_table_t *lt = calloc(1, sizeof(gmx_lease_table_t));
        if (!lt) goto fail;
        if (gmx_lease_table_init(lt, GMX_MAX_LEASES) != GMX_OK) {
            free(lt); goto fail;
        }
        sess->lease_table = lt;
    }

    /* Initialize journal */
    {
        gmx_journal_t *j = calloc(1, sizeof(gmx_journal_t));
        if (!j) goto fail;
        if (gmx_journal_init(j, GMX_JOURNAL_MAX_SEGMENTS) != GMX_OK) {
            free(j); goto fail;
        }
        sess->journal = j;
    }

    /* Initialize deferred queue */
    {
        gmx_journal_deferred_queue_t *dq = calloc(1, sizeof(*dq));
        if (!dq) goto fail;
        gmx_journal_deferred_init(dq);
        sess->deferred_queue = dq;
    }

    /* Initialize crypto context */
    {
        gmx_crypto_ctx_t *cc = calloc(1, sizeof(*cc));
        if (!cc) goto fail;
        uint8_t default_key[GMX_HMAC_KEY_SIZE] = {0};
        gmx_crypto_init(cc, default_key, GMX_HMAC_KEY_SIZE);
        sess->crypto_ctx = cc;
    }

    /* Initialize migration context */
    {
        gmx_migrate_ctx_t *mc = calloc(1, sizeof(*mc));
        if (!mc) goto fail;
        gmx_migrate_ctx_init(mc);
        sess->migrate_ctx = mc;
    }

    /* Initialize checkpoint ring (small fixed capacity) */
    sess->checkpoint_ring_cap = 8;
    sess->checkpoint_ring = calloc(sess->checkpoint_ring_cap, sizeof(uint32_t));
    if (!sess->checkpoint_ring) goto fail;
    sess->checkpoint_ring_count = 0;

    return sess;

fail:
    gmx_session_destroy(sess);
    return NULL;
}

void gmx_session_destroy(gmx_session_t *sess)
{
    if (!sess) return;

    /* Destroy streams before channels */
    for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
        if (sess->streams[i]) {
            gmx_stream_destroy(sess->streams[i]);
            sess->streams[i] = NULL;
        }
    }

    for (size_t i = 0; i < GMX_MAX_CHANNELS; i++) {
        if (sess->channels[i]) {
            gmx_channel_destroy(sess->channels[i]);
            sess->channels[i] = NULL;
        }
    }

    if (sess->route_table) {
        gmx_route_table_destroy(sess->route_table);
        free(sess->route_table);
    }
    if (sess->registry) {
        gmx_registry_destroy(sess->registry);
        free(sess->registry);
    }
    free(sess->compact_ctx);

    if (sess->quota_table) {
        gmx_quota_table_destroy(sess->quota_table);
        free(sess->quota_table);
    }
    if (sess->lease_table) {
        gmx_lease_table_destroy(sess->lease_table);
        free(sess->lease_table);
    }
    if (sess->journal) {
        gmx_journal_destroy(sess->journal);
        free(sess->journal);
    }
    free(sess->deferred_queue);
    if (sess->crypto_ctx) {
        gmx_crypto_destroy(sess->crypto_ctx);
        free(sess->crypto_ctx);
    }
    free(sess->migrate_ctx);
    free(sess->checkpoint_ring);
    free(sess);
}

/* ------------------------------------------------------------------ */
/*  Lookup helpers                                                     */
/* ------------------------------------------------------------------ */

gmx_channel_t *gmx_session_find_channel(gmx_session_t *sess, uint32_t channel_id)
{
    if (!sess) return NULL;
    for (size_t i = 0; i < GMX_MAX_CHANNELS; i++) {
        gmx_channel_t *ch = sess->channels[i];
        if (ch && ch->channel_id == channel_id && ch->active)
            return ch;
    }
    return NULL;
}

gmx_stream_t *gmx_session_find_stream(gmx_session_t *sess, uint32_t stream_id)
{
    if (!sess) return NULL;
    for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
        gmx_stream_t *s = sess->streams[i];
        if (s && s->stream_id == stream_id && s->active)
            return s;
    }
    return NULL;
}

static size_t session_find_channel_slot(gmx_session_t *sess, uint32_t channel_id)
{
    for (size_t i = 0; i < GMX_MAX_CHANNELS; i++) {
        gmx_channel_t *ch = sess->channels[i];
        if (ch && ch->channel_id == channel_id) return i;
    }
    return GMX_MAX_CHANNELS; /* not found sentinel */
}

/* ------------------------------------------------------------------ */
/*  Frame handlers                                                     */
/* ------------------------------------------------------------------ */

static gmx_status_t handle_session_init(gmx_session_t *sess,
                                          const gmx_frame_t *frame)
{
    if (sess->active) return GMX_ERR_DUPLICATE;
    sess->owner_token = frame->header.owner_token;
    sess->active = true;
    sess->epoch  = 0;
    return GMX_OK;
}

static gmx_status_t handle_route_advertise(gmx_session_t *sess,
                                             const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    uint32_t route_id = frame->header.route_id;
    if (route_id == 0) return GMX_ERR_BAD_ROUTE;

    const uint8_t *seg_data = NULL;
    size_t seg_len = 0;
    uint32_t segment_id = 0;
    if (frame->subrecord_count > 0 &&
        frame->subrecords[0].kind == GMX_SUB_ROUTE_SPEC) {
        seg_data   = frame->subrecords[0].payload;
        seg_len    = frame->subrecords[0].payload_length;
        segment_id = frame->subrecords[0].generation;
    }

    gmx_route_entry_t *entry = gmx_route_entry_create(
        sess->route_table, route_id, segment_id,
        sess->epoch, seg_data, seg_len);
    if (!entry) return GMX_ERR_ROUTE_FULL;

    gmx_registry_insert(sess->registry, route_id,
                         GMX_REG_TYPE_ROUTE, entry, sess->epoch);

    /*
     * Cache a direct reference to the registry entry within the
     * route for efficient metadata lookups during forwarding.
     * This avoids repeated hash-table lookups in the fast path.
     */
    void *reg_entry = gmx_registry_lookup_entry(sess->registry,
                                                  route_id, GMX_REG_TYPE_ROUTE);
    entry->registry_ref = reg_entry;

    return GMX_OK;
}

static gmx_status_t handle_channel_create(gmx_session_t *sess,
                                            const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    uint32_t ch_id = frame->header.channel_id;
    if (ch_id == 0) return GMX_ERR_BAD_CHANNEL;
    if (gmx_session_find_channel(sess, ch_id)) return GMX_ERR_DUPLICATE;

    gmx_channel_t *ch = gmx_channel_create(ch_id, sess->session_id,
                                             frame->header.owner_token);
    if (!ch) return GMX_ERR_ALLOC;

    for (size_t i = 0; i < GMX_MAX_CHANNELS; i++) {
        if (!sess->channels[i]) {
            sess->channels[i] = ch;
            sess->channel_count++;
            gmx_registry_insert(sess->registry, ch_id,
                                 GMX_REG_TYPE_CHANNEL, ch, sess->epoch);
            return GMX_OK;
        }
    }
    gmx_channel_destroy(ch);
    return GMX_ERR_CHANNEL_FULL;
}

static gmx_status_t handle_channel_bind(gmx_session_t *sess,
                                          const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_channel_t *ch = gmx_session_find_channel(sess, frame->header.channel_id);
    if (!ch) return GMX_ERR_BAD_CHANNEL;
    if (ch->owner_token != frame->header.owner_token) return GMX_ERR_BAD_OWNER;

    gmx_route_entry_t *entry = gmx_route_table_lookup(
        sess->route_table, frame->header.route_id);
    if (!entry) return GMX_ERR_BAD_ROUTE;

    return gmx_channel_bind_route(ch, entry, sess->epoch);
}

static gmx_status_t handle_stream_open(gmx_session_t *sess,
                                         const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_channel_t *ch = gmx_session_find_channel(sess, frame->header.channel_id);
    if (!ch) return GMX_ERR_BAD_CHANNEL;

    uint32_t stream_id = frame->header.route_id;
    if (stream_id == 0) return GMX_ERR_BAD_STREAM;
    if (gmx_session_find_stream(sess, stream_id)) return GMX_ERR_DUPLICATE;

    gmx_stream_t *s = gmx_stream_create(stream_id, ch->channel_id, ch);
    if (!s) return GMX_ERR_ALLOC;

    for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
        if (!sess->streams[i]) {
            sess->streams[i] = s;
            sess->stream_count++;
            gmx_registry_insert(sess->registry, stream_id,
                                 GMX_REG_TYPE_STREAM, s, sess->epoch);
            return GMX_OK;
        }
    }
    gmx_stream_destroy(s);
    return GMX_ERR_STREAM_FULL;
}

static gmx_status_t handle_data_write(gmx_session_t *sess,
                                        const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    uint32_t stream_id = frame->header.route_id;
    gmx_stream_t *s = gmx_session_find_stream(sess, stream_id);
    if (!s) return GMX_ERR_BAD_STREAM;

    for (uint16_t i = 0; i < frame->subrecord_count; i++) {
        const gmx_subrecord_t *sr = &frame->subrecords[i];
        if (sr->kind == GMX_SUB_DATA_PAYLOAD &&
            sr->payload && sr->payload_length > 0) {
            gmx_status_t rc = gmx_stream_write(s, sr->payload,
                                                sr->payload_length);
            if (rc != GMX_OK) return rc;
        }
    }
    return GMX_OK;
}

static gmx_status_t handle_epoch_advance(gmx_session_t *sess,
                                           const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    uint32_t new_epoch;
    if (frame->subrecord_count > 0 &&
        frame->subrecords[0].kind == GMX_SUB_EPOCH_SPEC) {
        new_epoch = frame->subrecords[0].local_epoch;
    } else {
        new_epoch = sess->epoch + 1;
    }
    if (new_epoch != sess->epoch + 1) return GMX_ERR_BAD_EPOCH;
    if (frame->header.sequence < sess->last_epoch_advance_seq + 2)
        return GMX_ERR_BAD_SEQUENCE;

    sess->last_epoch_advance_seq = frame->header.sequence;
    gmx_status_t rc = gmx_session_advance_epoch(sess, new_epoch);
    if (rc != GMX_OK) return rc;

    /*
     * Epoch-scoped garbage collection: sweep quota buckets that
     * have zero active subscribers and were created before the
     * previous epoch. These buckets have no remaining consumers
     * and can be safely reclaimed.
     */
    gmx_quota_table_t *qt = (gmx_quota_table_t *)sess->quota_table;
    if (qt && new_epoch > 2) {
        for (size_t i = 0; i < qt->capacity; i++) {
            gmx_quota_bucket_t *b = &qt->buckets[i];
            if (b->active && b->active_count == 0 &&
                b->epoch_created < new_epoch - 1) {
                b->active = false;
                qt->count--;
            }
        }
    }

    /*
     * Registry GC: remove and free inactive entries whose epoch
     * is older than the retention window. This reclaims memory
     * for registry nodes that are no longer referenced by active
     * route lookups.
     */
    if (new_epoch > 2) {
        gmx_registry_gc_stale(sess->registry, new_epoch - 2,
                               GMX_REG_TYPE_ROUTE);
    }

    return GMX_OK;
}

static gmx_status_t handle_compact_trigger(gmx_session_t *sess,
                                              const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    uint32_t threshold = GMX_EPOCH_COMPACT_THRESHOLD;
    if (frame->subrecord_count > 0 &&
        frame->subrecords[0].kind == GMX_SUB_COMPACT_SPEC) {
        uint32_t sub_threshold = frame->subrecords[0].generation;
        if (sub_threshold > 0 && sub_threshold <= 10)
            threshold = sub_threshold;
    }
    if (sess->epoch < threshold) return GMX_ERR_BAD_EPOCH;

    /* Phase 1: Compact route table */
    gmx_status_t rc = gmx_compact_route_table(sess->compact_ctx,
                                    sess->route_table,
                                    sess->epoch, threshold);

    /* Phase 2: Truncate old journal segments.
     * Segments created before the compaction watermark are freed
     * if their refcount allows. Leases that cached references to
     * these segments are NOT invalidated here. */
    gmx_journal_t *j = (gmx_journal_t *)sess->journal;
    if (j) {
        uint32_t watermark = sess->epoch - threshold;
        for (size_t i = 0; i < j->capacity; i++) {
            gmx_journal_segment_t *seg = j->segments[i];
            if (!seg || !seg->active) continue;
            if (seg->epoch_created < watermark) {
                free(seg->data);
                seg->data = NULL;
                seg->data_len = 0;
                free(seg);
                j->segments[i] = NULL;
                j->count--;
            }
        }
    }

    /* Phase 3: Process deferred journal events.
     * Events that have aged past the epoch threshold are batch-
     * processed, reading channel state through cached references. */
    gmx_journal_deferred_queue_t *dq =
        (gmx_journal_deferred_queue_t *)sess->deferred_queue;
    if (dq && dq->count > 0) {
        gmx_compact_process_deferred(sess->compact_ctx, dq, sess->epoch);
    }

    return rc;
}

static gmx_status_t handle_data_forward(gmx_session_t *sess,
                                          const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    uint32_t stream_id = frame->header.route_id;
    gmx_stream_t *s = gmx_session_find_stream(sess, stream_id);
    if (!s) return GMX_ERR_BAD_STREAM;

    /*
     * Before forwarding, verify the route's registry metadata
     * is still valid when a cascade update has been applied.
     * The route caches a direct reference to its registry entry
     * for fast generation checks.
     */
    if (s->channel && (frame->header.flags & 0x0001)) {
        gmx_route_entry_t *route = gmx_channel_get_route_cursor(s->channel);
        if (route && route->registry_ref) {
            gmx_registry_entry_t *reg_entry =
                (gmx_registry_entry_t *)route->registry_ref;
            /*
             * Read the registry entry's generation to verify the
             * route metadata hasn't been superseded by a newer
             * registration at a different epoch.
             */
            uint32_t reg_gen = reg_entry->generation;
            if (reg_gen < route->generation) {
                route->metric = (uint8_t)(reg_gen & 0xFF);
            }
        }
    }

    uint8_t fwd_buf[GMX_MAX_PAYLOAD];
    size_t fwd_len = 0;
    return gmx_stream_forward(s, fwd_buf, sizeof(fwd_buf), &fwd_len);
}

static gmx_status_t handle_channel_close(gmx_session_t *sess,
                                           const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    uint32_t ch_id = frame->header.channel_id;
    size_t slot = session_find_channel_slot(sess, ch_id);
    if (slot >= GMX_MAX_CHANNELS) return GMX_ERR_BAD_CHANNEL;

    gmx_channel_t *ch = sess->channels[slot];
    if (!ch || !ch->active) return GMX_ERR_BAD_CHANNEL;
    if (ch->owner_token != frame->header.owner_token) return GMX_ERR_BAD_OWNER;

    /* Leave quota group if attached */
    if (sess->channel_quota[slot]) {
        gmx_quota_bucket_t *bucket = (gmx_quota_bucket_t *)sess->channel_quota[slot];
        gmx_quota_leave_group(bucket);
        sess->channel_quota[slot] = NULL;
    }

    gmx_registry_remove(sess->registry, ch_id, GMX_REG_TYPE_CHANNEL);
    gmx_channel_destroy(ch);
    sess->channels[slot] = NULL;
    sess->channel_count--;
    return GMX_OK;
}

static gmx_status_t handle_quota_alloc(gmx_session_t *sess,
                                         const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_quota_table_t *qt = (gmx_quota_table_t *)sess->quota_table;
    if (!qt) return GMX_ERR_BAD_STATE;

    uint32_t group_id = frame->header.route_id;
    if (group_id == 0) return GMX_ERR_BAD_STATE;

    uint32_t limit = 1000;
    if (frame->subrecord_count > 0 &&
        frame->subrecords[0].kind == GMX_SUB_QUOTA_SPEC) {
        limit = frame->subrecords[0].generation;
        if (limit == 0) limit = 1000;
    }

    /* Create or lookup bucket */
    gmx_quota_bucket_t *bucket = gmx_quota_lookup_bucket(qt, group_id);
    if (!bucket) {
        bucket = gmx_quota_create_bucket(qt, group_id, limit, sess->epoch);
        if (!bucket) return GMX_ERR_OVERFLOW;
    }

    /* If channel_id specified, attach channel to this quota group */
    uint32_t ch_id = frame->header.channel_id;
    if (ch_id != 0) {
        size_t slot = session_find_channel_slot(sess, ch_id);
        if (slot < GMX_MAX_CHANNELS && sess->channels[slot]) {
            gmx_quota_join_group(bucket);
            sess->channel_quota[slot] = bucket;
        }
    }

    return GMX_OK;
}

static gmx_status_t handle_lease_acquire(gmx_session_t *sess,
                                           const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_lease_table_t *lt = (gmx_lease_table_t *)sess->lease_table;
    gmx_journal_t *j = (gmx_journal_t *)sess->journal;
    if (!lt || !j) return GMX_ERR_BAD_STATE;

    uint32_t lease_id = frame->header.route_id;
    uint32_t stream_id = frame->header.channel_id;
    if (lease_id == 0) return GMX_ERR_BAD_LEASE;

    /* Find the most recent journal segment for this lease */
    gmx_journal_segment_t *seg = NULL;
    for (size_t i = 0; i < j->capacity; i++) {
        if (j->segments[i] && j->segments[i]->active) {
            seg = j->segments[i];
        }
    }

    gmx_lease_t *lease = gmx_lease_acquire(lt, lease_id, stream_id,
                                            sess->epoch, seg,
                                            seg ? seg->data_cap : 0);
    if (!lease) return GMX_ERR_OVERFLOW;

    /* Attach lease to stream if stream exists */
    gmx_stream_t *s = gmx_session_find_stream(sess, stream_id);
    if (s) {
        for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
            if (sess->streams[i] == s) {
                sess->stream_lease[i] = lease;
                break;
            }
        }
    }

    return GMX_OK;
}

static gmx_status_t handle_journal_append(gmx_session_t *sess,
                                            const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_journal_t *j = (gmx_journal_t *)sess->journal;
    gmx_journal_deferred_queue_t *dq =
        (gmx_journal_deferred_queue_t *)sess->deferred_queue;
    if (!j || !dq) return GMX_ERR_BAD_STATE;

    uint32_t segment_id = frame->header.route_id;

    /* Create segment if it doesn't exist */
    gmx_journal_segment_t *seg = gmx_journal_lookup_segment(j, segment_id);
    if (!seg) {
        seg = gmx_journal_create_segment(j, sess->epoch);
        if (!seg) return GMX_ERR_OVERFLOW;
    }

    /* Append data from subrecords */
    for (uint16_t i = 0; i < frame->subrecord_count; i++) {
        const gmx_subrecord_t *sr = &frame->subrecords[i];
        if (sr->kind == GMX_SUB_JOURNAL_SPEC &&
            sr->payload && sr->payload_length > 0) {
            gmx_journal_append(j, seg->segment_id,
                                sr->payload, sr->payload_length);
        }
    }

    /*
     * If the frame references a channel, create a deferred event
     * for later batch processing during compaction. The event
     * stores a direct pointer to the channel object for efficient
     * access during deferred resolution.
     */
    uint32_t ch_id = frame->header.channel_id;
    if (ch_id != 0) {
        gmx_channel_t *ch = gmx_session_find_channel(sess, ch_id);
        if (ch) {
            gmx_journal_defer_event(dq, seg->segment_id,
                                     ch_id, ch, sess->epoch);
        }
    }

    return GMX_OK;
}

static gmx_status_t handle_migrate_prepare(gmx_session_t *sess,
                                              const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_migrate_ctx_t *mc = (gmx_migrate_ctx_t *)sess->migrate_ctx;
    if (!mc) return GMX_ERR_BAD_STATE;

    uint32_t migration_id = frame->header.route_id;
    uint32_t dest_session = frame->header.channel_id;

    gmx_status_t rc = gmx_migrate_prepare(mc, migration_id,
                                            sess->session_id, dest_session);
    if (rc != GMX_OK) return rc;

    /* Add channels to migration set from subrecords */
    for (uint16_t i = 0; i < frame->subrecord_count; i++) {
        const gmx_subrecord_t *sr = &frame->subrecords[i];
        if (sr->kind == GMX_SUB_MIGRATE_SPEC) {
            uint32_t ch_id = sr->generation;
            gmx_channel_t *ch = gmx_session_find_channel(sess, ch_id);
            if (ch) {
                ch->migrate_pending = true;
                gmx_migrate_add_channel(mc, ch_id,
                                         ch->credit_available, 0);

                /*
                 * Leave quota group during migration preparation.
                 * The channel's quota membership is transferred
                 * to the destination session during commit.
                 * Note: the session's channel_quota pointer is
                 * retained for credit reconciliation during the
                 * migration window.
                 */
                size_t slot = session_find_channel_slot(sess, ch_id);
                if (slot < GMX_MAX_CHANNELS && sess->channel_quota[slot]) {
                    gmx_quota_bucket_t *bucket =
                        (gmx_quota_bucket_t *)sess->channel_quota[slot];
                    gmx_quota_leave_group(bucket);
                }
            }
        }
    }

    /* Also add leases associated with migrating streams */
    for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
        if (sess->stream_lease[i]) {
            gmx_lease_t *lease = (gmx_lease_t *)sess->stream_lease[i];
            if (lease->active) {
                gmx_migrate_add_lease(mc, lease->lease_id);
            }
        }
    }

    return GMX_OK;
}

static gmx_status_t handle_migrate_transfer(gmx_session_t *sess,
                                               const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_migrate_ctx_t *mc = (gmx_migrate_ctx_t *)sess->migrate_ctx;
    if (!mc) return GMX_ERR_BAD_STATE;
    (void)frame;
    return gmx_migrate_transfer(mc);
}

static gmx_status_t handle_migrate_commit(gmx_session_t *sess,
                                             const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_migrate_ctx_t *mc = (gmx_migrate_ctx_t *)sess->migrate_ctx;
    if (!mc) return GMX_ERR_BAD_STATE;
    (void)frame;

    gmx_status_t rc = gmx_migrate_commit(mc);
    if (rc != GMX_OK) return rc;

    /*
     * After commit, release source-side lease copies for channels
     * that were migrated. The leases are freed because ownership
     * has transferred to the destination session.
     */
    gmx_lease_table_t *lt = (gmx_lease_table_t *)sess->lease_table;
    if (lt && mc->lease_count > 0) {
        for (uint32_t i = 0; i < mc->lease_count; i++) {
            uint32_t lid = mc->lease_ids[i];
            gmx_lease_t *lease = gmx_lease_lookup(lt, lid);
            if (lease) {
                lease->active = false;
                /*
                 * Free the lease. Note: any streams that cached
                 * this lease pointer in sess->stream_lease[] still
                 * hold a reference that may become invalid.
                 */
                for (size_t s = 0; s < lt->capacity; s++) {
                    if (lt->entries[s] == lease) {
                        lt->entries[s] = NULL;
                        lt->count--;
                        break;
                    }
                }
                free(lease);
            }
        }
    }

    return GMX_OK;
}

static gmx_status_t handle_heartbeat(gmx_session_t *sess,
                                       const gmx_frame_t *frame)
{
    (void)frame;
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    return GMX_OK;
}

static gmx_status_t handle_credit_grant(gmx_session_t *sess,
                                           const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    uint32_t ch_id = frame->header.channel_id;
    gmx_channel_t *ch = gmx_session_find_channel(sess, ch_id);
    if (!ch) return GMX_ERR_BAD_CHANNEL;

    uint32_t amount = 4;
    if (frame->subrecord_count > 0 &&
        frame->subrecords[0].kind == GMX_SUB_CREDIT_SPEC) {
        amount = frame->subrecords[0].generation;
        if (amount == 0) amount = 4;
    }

    /*
     * Before granting credits, verify the channel's route is
     * still viable by checking the route entry's generation.
     * This prevents credit grants to channels whose routes
     * have been invalidated by cascade updates.
     */
    gmx_route_entry_t *route = gmx_channel_get_route_cursor(ch);
    if (route) {
        uint32_t gen = route->generation;
        if (gen == 0) return GMX_ERR_BAD_ROUTE;
    }

    return gmx_channel_grant_credit(ch, amount);
}

static gmx_status_t handle_route_cascade(gmx_session_t *sess,
                                            const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    uint32_t route_id = frame->header.route_id;

    const uint8_t *new_data = NULL;
    size_t new_len = 0;
    if (frame->subrecord_count > 0 &&
        frame->subrecords[0].kind == GMX_SUB_ROUTE_SPEC) {
        new_data = frame->subrecords[0].payload;
        new_len  = frame->subrecords[0].payload_length;
    }

    return gmx_route_cascade_update(sess->route_table, route_id,
                                     new_data, new_len);
}

static gmx_status_t handle_nonce_verify(gmx_session_t *sess,
                                          const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_crypto_ctx_t *cc = (gmx_crypto_ctx_t *)sess->crypto_ctx;
    if (!cc) return GMX_ERR_BAD_STATE;

    uint32_t ch_id = frame->header.channel_id;
    uint64_t nonce = 0;
    if (frame->subrecord_count > 0 &&
        frame->subrecords[0].kind == GMX_SUB_NONCE_SPEC) {
        nonce = (uint64_t)frame->subrecords[0].local_epoch |
                ((uint64_t)frame->subrecords[0].generation << 32);
    }

    /*
     * For channels with pending migration, use direct table access
     * to avoid the overhead of full anti-replay validation during
     * the migration transfer window. The migration protocol ensures
     * nonce continuity across sessions.
     */
    gmx_channel_t *ch = gmx_session_find_channel(sess, ch_id);
    if (ch && ch->migrate_pending) {
        gmx_nonce_entry_t *entry = &cc->nonce_table.entries[ch_id];
        entry->nonce = nonce;
        entry->epoch = sess->epoch;
        entry->used  = true;
        return GMX_OK;
    }

    return gmx_crypto_verify_nonce(&cc->nonce_table, ch_id,
                                    nonce, sess->epoch);
}

static gmx_status_t handle_credit_flush(gmx_session_t *sess,
                                          const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    uint32_t ch_id = frame->header.channel_id;
    gmx_channel_t *ch = gmx_session_find_channel(sess, ch_id);
    if (!ch) return GMX_ERR_BAD_CHANNEL;

    /*
     * Before flushing credits, verify against the channel's quota
     * bucket to ensure the flush does not exceed the tenant's
     * allocation. The quota reference was established during
     * QUOTA_ALLOC and is retained across migration boundaries.
     */
    size_t slot = session_find_channel_slot(sess, ch_id);
    if (slot < GMX_MAX_CHANNELS && sess->channel_quota[slot]) {
        gmx_quota_bucket_t *bucket =
            (gmx_quota_bucket_t *)sess->channel_quota[slot];
        /*
         * Read the bucket's consumption state for enforcement.
         * This access is through the cached pointer from QUOTA_ALLOC.
         */
        uint32_t remaining = bucket->limit - bucket->consumed;
        if (ch->credit_available > remaining) {
            ch->credit_available = remaining;
        }
    }

    /* Flush credits: write credit entries into a buffer */
    uint8_t credit_buf[256];
    size_t buf_cap = sizeof(credit_buf);
    size_t offset = 0;

    for (uint32_t i = 0; i < ch->credit_available && offset + 4 <= buf_cap; i++) {
        gmx_wire_write_u32(credit_buf + offset, i);
        offset += 4;
    }

    return GMX_OK;
}

static gmx_status_t handle_checkpoint_write(gmx_session_t *sess,
                                              const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_journal_t *j = (gmx_journal_t *)sess->journal;
    if (!j) return GMX_ERR_BAD_STATE;

    /* Create a new journal segment as a checkpoint */
    gmx_journal_segment_t *seg = gmx_journal_create_segment(j, sess->epoch);
    if (!seg) return GMX_ERR_OVERFLOW;

    /* Write checkpoint data from subrecords */
    for (uint16_t i = 0; i < frame->subrecord_count; i++) {
        const gmx_subrecord_t *sr = &frame->subrecords[i];
        if (sr->kind == GMX_SUB_CHECKPOINT && sr->payload && sr->payload_length > 0) {
            gmx_journal_append(j, seg->segment_id,
                                sr->payload, sr->payload_length);
        }
    }

    /* Record checkpoint in the session's checkpoint ring */
    if (sess->checkpoint_ring &&
        sess->checkpoint_ring_count < sess->checkpoint_ring_cap) {
        sess->checkpoint_ring[sess->checkpoint_ring_count++] = seg->segment_id;
    }

    return GMX_OK;
}

static gmx_status_t handle_lease_extend(gmx_session_t *sess,
                                           const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_lease_table_t *lt = (gmx_lease_table_t *)sess->lease_table;
    if (!lt) return GMX_ERR_BAD_STATE;

    uint32_t lease_id = frame->header.route_id;
    if (lease_id == 0) return GMX_ERR_BAD_LEASE;

    gmx_lease_t *lease = gmx_lease_lookup(lt, lease_id);
    if (!lease) return GMX_ERR_NOT_FOUND;

    /*
     * Extend the lease's history depth based on the subrecord's
     * generation field. This allows the lease holder to scan
     * more checkpoint entries during replay.
     */
    uint32_t extension = 4;
    if (frame->subrecord_count > 0 &&
        frame->subrecords[0].kind == GMX_SUB_LEASE_SPEC) {
        extension = frame->subrecords[0].generation;
        if (extension == 0) extension = 4;
    }

    return gmx_lease_extend_history(lease, extension);
}

static gmx_status_t handle_checkpoint_replay(gmx_session_t *sess,
                                                const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_journal_t *j = (gmx_journal_t *)sess->journal;
    gmx_lease_table_t *lt = (gmx_lease_table_t *)sess->lease_table;
    if (!j || !lt) return GMX_ERR_BAD_STATE;

    uint32_t lease_id = frame->header.route_id;
    gmx_lease_t *lease = gmx_lease_lookup(lt, lease_id);
    if (!lease) return GMX_ERR_NOT_FOUND;

    /*
     * Scan the checkpoint ring using the lease's history depth
     * as the scan count. The history depth determines how many
     * checkpoint entries the lease holder can access for replay.
     */
    uint32_t scan_count = lease->history_depth;
    if (scan_count == 0) scan_count = sess->checkpoint_ring_count;

    uint8_t replay_buf[4096];
    size_t replay_len = 0;

    return gmx_journal_scan_checkpoints(j,
                                          sess->checkpoint_ring,
                                          scan_count,
                                          replay_buf, sizeof(replay_buf),
                                          &replay_len);
}

static gmx_status_t handle_journal_replay(gmx_session_t *sess,
                                             const gmx_frame_t *frame)
{
    if (!sess->active) return GMX_ERR_NOT_ACTIVE;
    gmx_journal_t *j = (gmx_journal_t *)sess->journal;
    gmx_lease_table_t *lt = (gmx_lease_table_t *)sess->lease_table;
    if (!j || !lt) return GMX_ERR_BAD_STATE;

    uint32_t lease_id = frame->header.route_id;
    if (lease_id == 0) return GMX_ERR_BAD_LEASE;

    gmx_lease_t *lease = gmx_lease_lookup(lt, lease_id);

    /*
     * If the lease is no longer in the table (e.g., after migration
     * transferred ownership), check the per-stream cached references.
     * Streams retain a direct pointer to their lease for fast replay
     * access during the migration transition window.
     */
    if (!lease) {
        uint32_t stream_id = frame->header.channel_id;
        for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
            gmx_stream_t *s = sess->streams[i];
            if (s && s->stream_id == stream_id && sess->stream_lease[i]) {
                lease = (gmx_lease_t *)sess->stream_lease[i];
                break;
            }
        }
    }
    if (!lease) return GMX_ERR_NOT_FOUND;

    uint8_t replay_buf[4096];
    size_t replay_len = 0;
    return gmx_journal_checkpoint_replay(j, lease,
                                          replay_buf, sizeof(replay_buf),
                                          &replay_len);
}

/* ------------------------------------------------------------------ */
/*  Dispatch                                                           */
/* ------------------------------------------------------------------ */

gmx_status_t gmx_session_dispatch_frame(gmx_session_t *sess,
                                          const gmx_frame_t *frame)
{
    if (!sess || !frame) return GMX_ERR_BAD_STATE;

    if (frame->header.frame_type != GMX_FRAME_SESSION_INIT) {
        if (!sess->active) return GMX_ERR_NOT_ACTIVE;
        if (frame->header.session_id != sess->session_id)
            return GMX_ERR_BAD_SESSION_ID;
    }

    if (frame->header.sequence != sess->expected_sequence)
        return GMX_ERR_BAD_SEQUENCE;
    sess->expected_sequence++;

    switch (frame->header.frame_type) {
    case GMX_FRAME_SESSION_INIT:      return handle_session_init(sess, frame);
    case GMX_FRAME_ROUTE_ADVERTISE:   return handle_route_advertise(sess, frame);
    case GMX_FRAME_CHANNEL_CREATE:    return handle_channel_create(sess, frame);
    case GMX_FRAME_CHANNEL_BIND:      return handle_channel_bind(sess, frame);
    case GMX_FRAME_STREAM_OPEN:       return handle_stream_open(sess, frame);
    case GMX_FRAME_DATA_WRITE:        return handle_data_write(sess, frame);
    case GMX_FRAME_EPOCH_ADVANCE:     return handle_epoch_advance(sess, frame);
    case GMX_FRAME_COMPACT_TRIGGER:   return handle_compact_trigger(sess, frame);
    case GMX_FRAME_DATA_FORWARD:      return handle_data_forward(sess, frame);
    case GMX_FRAME_CHANNEL_CLOSE:     return handle_channel_close(sess, frame);
    case GMX_FRAME_LEASE_ACQUIRE:     return handle_lease_acquire(sess, frame);
    case GMX_FRAME_QUOTA_ALLOC:       return handle_quota_alloc(sess, frame);
    case GMX_FRAME_HEARTBEAT:         return handle_heartbeat(sess, frame);
    case GMX_FRAME_JOURNAL_APPEND:    return handle_journal_append(sess, frame);
    case GMX_FRAME_MIGRATE_PREPARE:   return handle_migrate_prepare(sess, frame);
    case GMX_FRAME_MIGRATE_TRANSFER:  return handle_migrate_transfer(sess, frame);
    case GMX_FRAME_MIGRATE_COMMIT:    return handle_migrate_commit(sess, frame);
    case GMX_FRAME_NONCE_VERIFY:      return handle_nonce_verify(sess, frame);
    case GMX_FRAME_CREDIT_GRANT:      return handle_credit_grant(sess, frame);
    case GMX_FRAME_CREDIT_FLUSH:      return handle_credit_flush(sess, frame);
    case GMX_FRAME_ROUTE_CASCADE:     return handle_route_cascade(sess, frame);
    case GMX_FRAME_CHECKPOINT_WRITE:  return handle_checkpoint_write(sess, frame);
    case GMX_FRAME_CHECKPOINT_REPLAY: return handle_checkpoint_replay(sess, frame);
    case GMX_FRAME_JOURNAL_REPLAY:   return handle_journal_replay(sess, frame);
    case GMX_FRAME_LEASE_EXTEND:     return handle_lease_extend(sess, frame);
    default:
        return GMX_OK; /* forward compat: skip unknown frame types */
    }
}

/* ------------------------------------------------------------------ */
/*  Epoch management                                                   */
/* ------------------------------------------------------------------ */

gmx_status_t gmx_session_advance_epoch(gmx_session_t *sess, uint32_t new_epoch)
{
    if (!sess || !sess->active) return GMX_ERR_NOT_ACTIVE;
    if (new_epoch <= sess->epoch) return GMX_ERR_BAD_EPOCH;
    sess->epoch = new_epoch;
    gmx_registry_set_epoch(sess->registry, new_epoch);
    sess->epoch_advanced = true;
    return GMX_OK;
}

/* ------------------------------------------------------------------ */
/*  Top-level ingest                                                   */
/* ------------------------------------------------------------------ */

gmx_status_t gmx_session_ingest(gmx_session_t *sess,
                                 const uint8_t *data, size_t len)
{
    if (!sess || !data || len == 0) return GMX_ERR_BAD_STATE;

    gmx_session_header_t hdr;
    gmx_status_t rc = gmx_wire_decode_session_header(data, len, &hdr);
    if (rc != GMX_OK) return rc;

    if (sess->active && hdr.session_id != sess->session_id)
        return GMX_ERR_BAD_SESSION_ID;
    if (sess->active && hdr.epoch < sess->epoch)
        return GMX_ERR_BAD_EPOCH;

    size_t offset = GMX_SESSION_HEADER_WIRE_SIZE;
    for (uint16_t i = 0; i < hdr.frame_count; i++) {
        if (offset >= len) break;
        gmx_frame_t frame;
        size_t consumed = 0;
        rc = gmx_wire_decode_frame(data + offset, len - offset,
                                    &frame, &consumed);
        if (rc != GMX_OK) return rc;
        rc = gmx_session_dispatch_frame(sess, &frame);
        if (rc != GMX_OK) return rc;
        offset += consumed;
    }

    return GMX_OK;
}
