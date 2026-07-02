/*
 * libgridmux - Session state validation and consistency checks
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Provides deep validation of session state for debugging,
 * migration verification, and invariant checking. These functions
 * are called during development builds to catch state corruption.
 */
#include "gmx/session.h"
#include "gmx/channel.h"
#include "gmx/stream.h"
#include "gmx/route.h"
#include "gmx/registry.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

typedef struct gmx_session_validation_result {
    uint32_t channels_ok;
    uint32_t channels_err;
    uint32_t streams_ok;
    uint32_t streams_err;
    uint32_t routes_ok;
    uint32_t routes_err;
    uint32_t orphan_streams;
    uint32_t registry_inconsistencies;
    bool     all_ok;
} gmx_session_validation_result_t;

/*
 * Validate all channels in the session.
 * Checks owner tokens, active flags, and registry consistency.
 */
static void validate_channels(gmx_session_t *sess,
                               gmx_session_validation_result_t *result)
{
    for (size_t i = 0; i < GMX_MAX_CHANNELS; i++) {
        gmx_channel_t *ch = sess->channels[i];
        if (!ch) continue;

        if (ch->active && ch->session_id == sess->session_id) {
            /* Check registry entry exists */
            void *reg_obj = gmx_registry_lookup(sess->registry,
                                                  ch->channel_id,
                                                  GMX_REG_TYPE_CHANNEL);
            if (reg_obj == ch) {
                result->channels_ok++;
            } else {
                result->channels_err++;
                result->registry_inconsistencies++;
            }
        } else if (!ch->active) {
            result->channels_ok++;
        } else {
            result->channels_err++;
        }
    }
}

/*
 * Validate all streams in the session.
 * Checks channel references and orphan detection.
 */
static void validate_streams(gmx_session_t *sess,
                              gmx_session_validation_result_t *result)
{
    for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
        gmx_stream_t *s = sess->streams[i];
        if (!s) continue;

        if (s->active) {
            /* Verify channel reference is valid */
            gmx_channel_t *ch = gmx_session_find_channel(sess, s->channel_id);
            if (ch && ch->active) {
                result->streams_ok++;
            } else {
                result->orphan_streams++;
                result->streams_err++;
            }

            /* Check registry */
            void *reg_obj = gmx_registry_lookup(sess->registry,
                                                  s->stream_id,
                                                  GMX_REG_TYPE_STREAM);
            if (reg_obj != s) {
                result->registry_inconsistencies++;
            }
        } else {
            result->streams_ok++;
        }
    }
}

/*
 * Validate route table entries.
 */
static void validate_routes(gmx_session_t *sess,
                             gmx_session_validation_result_t *result)
{
    if (!sess->route_table) return;

    for (size_t i = 0; i < sess->route_table->capacity; i++) {
        gmx_route_entry_t *entry = sess->route_table->entries[i];
        if (!entry) continue;

        if (entry->active) {
            if (entry->epoch_created <= sess->epoch) {
                result->routes_ok++;
            } else {
                result->routes_err++;
            }
        } else {
            result->routes_ok++;
        }
    }
}

gmx_status_t gmx_session_validate(gmx_session_t *sess,
                                     gmx_session_validation_result_t *result)
{
    if (!sess || !result) return GMX_ERR_BAD_STATE;
    memset(result, 0, sizeof(*result));

    validate_channels(sess, result);
    validate_streams(sess, result);
    validate_routes(sess, result);

    result->all_ok = (result->channels_err == 0 &&
                      result->streams_err == 0 &&
                      result->routes_err == 0 &&
                      result->orphan_streams == 0 &&
                      result->registry_inconsistencies == 0);

    return GMX_OK;
}

/*
 * Count total objects managed by the session.
 */
gmx_status_t gmx_session_object_counts(const gmx_session_t *sess,
                                          uint32_t *channels,
                                          uint32_t *streams,
                                          uint32_t *routes)
{
    if (!sess) return GMX_ERR_BAD_STATE;

    if (channels) {
        *channels = 0;
        for (size_t i = 0; i < GMX_MAX_CHANNELS; i++) {
            if (sess->channels[i] && sess->channels[i]->active) (*channels)++;
        }
    }
    if (streams) {
        *streams = 0;
        for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
            if (sess->streams[i] && sess->streams[i]->active) (*streams)++;
        }
    }
    if (routes && sess->route_table) {
        *routes = 0;
        for (size_t i = 0; i < sess->route_table->capacity; i++) {
            if (sess->route_table->entries[i] &&
                sess->route_table->entries[i]->active) (*routes)++;
        }
    }

    return GMX_OK;
}

/*
 * Check if a session is in a clean state (no active objects).
 */
bool gmx_session_is_clean(const gmx_session_t *sess)
{
    if (!sess || !sess->active) return true;

    for (size_t i = 0; i < GMX_MAX_CHANNELS; i++) {
        if (sess->channels[i] && sess->channels[i]->active) return false;
    }
    for (size_t i = 0; i < GMX_MAX_STREAMS; i++) {
        if (sess->streams[i] && sess->streams[i]->active) return false;
    }
    return true;
}

/*
 * Compute the expected sequence number based on session state.
 */
uint32_t gmx_session_expected_sequence(const gmx_session_t *sess)
{
    return sess ? sess->expected_sequence : 0;
}
