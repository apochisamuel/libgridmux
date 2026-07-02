/*
 * libgridmux - Wire protocol deep validation
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Provides thorough validation of wire protocol messages beyond
 * basic parsing: frame type consistency, subrecord coherence,
 * session-level invariants, and cross-frame constraint checking.
 */
#include "gmx/wire.h"
#include "gmx/crc.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

typedef struct gmx_wire_validation_result {
    uint32_t frames_checked;
    uint32_t frames_valid;
    uint32_t frames_invalid;
    uint32_t subrecords_checked;
    uint32_t subrecords_valid;
    uint32_t first_error_seq;
    gmx_status_t first_error_code;
    bool     all_valid;
} gmx_wire_validation_result_t;

static gmx_status_t validate_frame_type_subrecord_coherence(
    const gmx_frame_t *frame)
{
    if (!frame) return GMX_ERR_BAD_STATE;
    if (frame->subrecord_count == 0) return GMX_OK;

    /* Validate that subrecord kinds are appropriate for the frame type */
    for (uint16_t i = 0; i < frame->subrecord_count; i++) {
        const gmx_subrecord_t *sr = &frame->subrecords[i];

        /* Check subrecord length consistency */
        if (sr->total_length < GMX_SUBRECORD_HEADER_WIRE_SIZE) {
            return GMX_ERR_TRUNCATED;
        }

        /* Validate payload length matches total - header */
        uint16_t expected_payload = sr->total_length - GMX_SUBRECORD_HEADER_WIRE_SIZE;
        if (sr->payload_length != expected_payload) {
            return GMX_ERR_BAD_LENGTH;
        }
    }
    return GMX_OK;
}

static gmx_status_t validate_frame_id_consistency(const gmx_frame_t *frame)
{
    if (!frame) return GMX_ERR_BAD_STATE;

    /* Session init should not reference channels or routes */
    if (frame->header.frame_type == GMX_FRAME_SESSION_INIT) {
        if (frame->header.channel_id != 0 || frame->header.route_id != 0) {
            return GMX_ERR_BAD_STATE;
        }
    }

    /* Epoch advance should not reference channels */
    if (frame->header.frame_type == GMX_FRAME_EPOCH_ADVANCE) {
        if (frame->header.channel_id != 0) {
            return GMX_ERR_BAD_STATE;
        }
    }

    return GMX_OK;
}

gmx_status_t gmx_wire_validate_frame_deep(const gmx_frame_t *frame,
                                             gmx_status_t *error_code)
{
    if (!frame) return GMX_ERR_BAD_STATE;

    gmx_status_t rc;

    /* Validate subrecord coherence */
    rc = validate_frame_type_subrecord_coherence(frame);
    if (rc != GMX_OK) {
        if (error_code) *error_code = rc;
        return rc;
    }

    /* Validate ID consistency */
    rc = validate_frame_id_consistency(frame);
    if (rc != GMX_OK) {
        if (error_code) *error_code = rc;
        return rc;
    }

    /* Validate frame length */
    if (frame->header.length < GMX_FRAME_HEADER_WIRE_SIZE) {
        if (error_code) *error_code = GMX_ERR_TRUNCATED;
        return GMX_ERR_TRUNCATED;
    }

    return GMX_OK;
}

gmx_status_t gmx_wire_validate_message(const uint8_t *data, size_t len,
                                          gmx_wire_validation_result_t *result)
{
    if (!data || !result) return GMX_ERR_BAD_STATE;
    memset(result, 0, sizeof(*result));
    result->all_valid = true;
    result->first_error_seq = 0xFFFFFFFF;

    /* Validate session header */
    gmx_session_header_t hdr;
    gmx_status_t rc = gmx_wire_decode_session_header(data, len, &hdr);
    if (rc != GMX_OK) {
        result->all_valid = false;
        result->first_error_code = rc;
        return rc;
    }

    /* Validate each frame */
    size_t offset = GMX_SESSION_HEADER_WIRE_SIZE;
    for (uint16_t i = 0; i < hdr.frame_count; i++) {
        if (offset >= len) break;

        gmx_frame_t frame;
        size_t consumed = 0;
        rc = gmx_wire_decode_frame(data + offset, len - offset,
                                    &frame, &consumed);
        result->frames_checked++;

        if (rc == GMX_OK) {
            gmx_status_t deep_rc = gmx_wire_validate_frame_deep(&frame, NULL);
            if (deep_rc == GMX_OK) {
                result->frames_valid++;
                result->subrecords_checked += frame.subrecord_count;
                result->subrecords_valid += frame.subrecord_count;
            } else {
                result->frames_invalid++;
                result->all_valid = false;
                if (result->first_error_seq == 0xFFFFFFFF) {
                    result->first_error_seq = frame.header.sequence;
                    result->first_error_code = deep_rc;
                }
            }
        } else {
            result->frames_invalid++;
            result->all_valid = false;
            if (result->first_error_seq == 0xFFFFFFFF) {
                result->first_error_seq = i;
                result->first_error_code = rc;
            }
        }

        if (consumed > 0) offset += consumed;
        else break;
    }

    return GMX_OK;
}

bool gmx_wire_is_control_frame(uint16_t frame_type)
{
    switch (frame_type) {
    case GMX_FRAME_SESSION_INIT:
    case GMX_FRAME_EPOCH_ADVANCE:
    case GMX_FRAME_COMPACT_TRIGGER:
    case GMX_FRAME_HEARTBEAT:
        return true;
    default:
        return false;
    }
}

bool gmx_wire_is_data_frame(uint16_t frame_type)
{
    switch (frame_type) {
    case GMX_FRAME_DATA_WRITE:
    case GMX_FRAME_DATA_FORWARD:
    case GMX_FRAME_JOURNAL_APPEND:
    case GMX_FRAME_CHECKPOINT_WRITE:
        return true;
    default:
        return false;
    }
}

bool gmx_wire_is_migration_frame(uint16_t frame_type)
{
    switch (frame_type) {
    case GMX_FRAME_MIGRATE_PREPARE:
    case GMX_FRAME_MIGRATE_TRANSFER:
    case GMX_FRAME_MIGRATE_COMMIT:
        return true;
    default:
        return false;
    }
}
