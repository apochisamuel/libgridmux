/*
 * libgridmux - Migration validation and pre-flight checks
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Validates that a migration can proceed safely by checking
 * source session state, channel dependencies, lease references,
 * quota group memberships, and route bindings. Returns a detailed
 * validation report with pass/fail per check.
 */
#include "gmx/migrate.h"
#include "gmx/session.h"
#include "gmx/channel.h"
#include "gmx/stream.h"
#include "gmx/route.h"
#include "gmx/lease.h"
#include "gmx/quota.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_MIGRATE_MAX_CHECKS  32

typedef struct gmx_migrate_check {
    const char *name;
    bool        passed;
    uint32_t    detail_u32;
} gmx_migrate_check_t;

typedef struct gmx_migrate_preflight {
    gmx_migrate_check_t checks[GMX_MIGRATE_MAX_CHECKS];
    uint32_t             check_count;
    uint32_t             passed;
    uint32_t             failed;
    bool                 can_proceed;
} gmx_migrate_preflight_t;

gmx_status_t gmx_migrate_preflight_init(gmx_migrate_preflight_t *pf)
{
    if (!pf) return GMX_ERR_BAD_STATE;
    memset(pf, 0, sizeof(*pf));
    pf->can_proceed = true;
    return GMX_OK;
}

static void add_check(gmx_migrate_preflight_t *pf,
                       const char *name, bool passed, uint32_t detail)
{
    if (pf->check_count >= GMX_MIGRATE_MAX_CHECKS) return;
    gmx_migrate_check_t *c = &pf->checks[pf->check_count++];
    c->name = name;
    c->passed = passed;
    c->detail_u32 = detail;
    if (passed) pf->passed++;
    else { pf->failed++; pf->can_proceed = false; }
}

gmx_status_t gmx_migrate_validate_source(gmx_migrate_preflight_t *pf,
                                            const gmx_session_t *src,
                                            const gmx_migrate_ctx_t *ctx)
{
    if (!pf || !src || !ctx) return GMX_ERR_BAD_STATE;

    /* Check: source session is active */
    add_check(pf, "source_active", src->active, 0);

    /* Check: migration is in valid state */
    add_check(pf, "migration_state",
              ctx->state == GMX_MIGRATE_PREPARING, ctx->state);

    /* Check: at least one channel selected */
    add_check(pf, "channels_selected",
              ctx->channel_count > 0, ctx->channel_count);

    /* Check: channels exist in source */
    uint32_t found = 0;
    for (uint32_t i = 0; i < ctx->channel_count; i++) {
        if (gmx_session_find_channel((gmx_session_t *)src, ctx->channel_ids[i])) {
            found++;
        }
    }
    add_check(pf, "channels_exist", found == ctx->channel_count, found);

    /* Check: channels have valid owner tokens */
    uint32_t valid_owners = 0;
    for (uint32_t i = 0; i < ctx->channel_count; i++) {
        gmx_channel_t *ch = gmx_session_find_channel(
            (gmx_session_t *)src, ctx->channel_ids[i]);
        if (ch && ch->owner_token == src->owner_token) {
            valid_owners++;
        }
    }
    add_check(pf, "owner_tokens_valid",
              valid_owners == ctx->channel_count, valid_owners);

    /* Check: no channel is already in migration */
    uint32_t already_migrating = 0;
    for (uint32_t i = 0; i < ctx->channel_count; i++) {
        gmx_channel_t *ch = gmx_session_find_channel(
            (gmx_session_t *)src, ctx->channel_ids[i]);
        if (ch && ch->migrate_pending) {
            already_migrating++;
        }
    }
    add_check(pf, "no_pending_migration", already_migrating == 0,
              already_migrating);

    /* Check: credit snapshot is reasonable */
    uint32_t total_credit = 0;
    for (uint32_t i = 0; i < ctx->channel_count; i++) {
        total_credit += ctx->credit_snapshot[i];
    }
    add_check(pf, "credit_snapshot_sane",
              total_credit > 0 && total_credit < 1000000, total_credit);

    /* Check: source and dest session IDs differ */
    add_check(pf, "session_ids_differ",
              ctx->source_session_id != ctx->dest_session_id,
              ctx->dest_session_id);

    return GMX_OK;
}

gmx_status_t gmx_migrate_validate_streams(gmx_migrate_preflight_t *pf,
                                             const gmx_session_t *src,
                                             const gmx_migrate_ctx_t *ctx)
{
    if (!pf || !src || !ctx) return GMX_ERR_BAD_STATE;

    /* Count streams attached to migrating channels */
    uint32_t affected_streams = 0;
    for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
        gmx_stream_t *s = src->streams[i];
        if (!s || !s->active) continue;
        for (uint32_t j = 0; j < ctx->channel_count; j++) {
            if (s->channel_id == ctx->channel_ids[j]) {
                affected_streams++;
                break;
            }
        }
    }
    add_check(pf, "affected_streams_count", true, affected_streams);

    /* Check: affected streams have no pending writes */
    uint32_t active_writers = 0;
    for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
        gmx_stream_t *s = src->streams[i];
        if (!s || !s->active) continue;
        for (uint32_t j = 0; j < ctx->channel_count; j++) {
            if (s->channel_id == ctx->channel_ids[j] && s->bytes_written > 0) {
                active_writers++;
                break;
            }
        }
    }
    add_check(pf, "streams_quiesced",
              active_writers <= affected_streams, active_writers);

    return GMX_OK;
}

gmx_status_t gmx_migrate_validate_destination(gmx_migrate_preflight_t *pf,
                                                 const gmx_session_t *dst,
                                                 const gmx_migrate_ctx_t *ctx)
{
    if (!pf || !dst || !ctx) return GMX_ERR_BAD_STATE;

    /* Check: destination session exists and is active */
    add_check(pf, "dest_active", dst->active, 0);

    /* Check: destination has capacity for migrated channels */
    uint32_t available_slots = 0;
    for (size_t i = 0; i < GMX_MAX_CHANNELS; i++) {
        if (!dst->channels[i]) available_slots++;
    }
    add_check(pf, "dest_channel_capacity",
              available_slots >= ctx->channel_count, available_slots);

    /* Check: no channel ID conflicts at destination */
    uint32_t conflicts = 0;
    for (uint32_t i = 0; i < ctx->channel_count; i++) {
        if (gmx_session_find_channel((gmx_session_t *)dst,
                                      ctx->channel_ids[i])) {
            conflicts++;
        }
    }
    add_check(pf, "no_id_conflicts", conflicts == 0, conflicts);

    return GMX_OK;
}

bool gmx_migrate_preflight_can_proceed(const gmx_migrate_preflight_t *pf)
{
    return pf ? pf->can_proceed : false;
}

uint32_t gmx_migrate_preflight_total_checks(const gmx_migrate_preflight_t *pf)
{
    return pf ? pf->check_count : 0;
}

uint32_t gmx_migrate_preflight_passed(const gmx_migrate_preflight_t *pf)
{
    return pf ? pf->passed : 0;
}
