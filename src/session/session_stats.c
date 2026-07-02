/*
 * libgridmux - Session statistics and monitoring
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/session.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

typedef struct gmx_session_stats {
    uint32_t frames_processed;
    uint32_t frames_rejected;
    uint32_t bytes_processed;
    uint32_t epoch_advances;
    uint32_t compactions_run;
    uint32_t channels_created;
    uint32_t channels_closed;
    uint32_t streams_opened;
    uint32_t streams_closed;
    uint32_t migrations_started;
    uint32_t migrations_completed;
    uint32_t crc_failures;
    uint32_t sequence_failures;
    float    avg_frame_size;
} gmx_session_stats_t;

gmx_status_t gmx_session_stats_init(gmx_session_stats_t *stats)
{
    if (!stats) return GMX_ERR_BAD_STATE;
    memset(stats, 0, sizeof(*stats));
    return GMX_OK;
}

gmx_status_t gmx_session_stats_record_frame(gmx_session_stats_t *stats,
                                               uint32_t frame_size,
                                               bool accepted)
{
    if (!stats) return GMX_ERR_BAD_STATE;
    if (accepted) {
        stats->frames_processed++;
        stats->bytes_processed += frame_size;
        if (stats->frames_processed > 0) {
            stats->avg_frame_size = (float)stats->bytes_processed /
                                    (float)stats->frames_processed;
        }
    } else {
        stats->frames_rejected++;
    }
    return GMX_OK;
}

gmx_status_t gmx_session_stats_record_event(gmx_session_stats_t *stats,
                                                uint16_t event_type)
{
    if (!stats) return GMX_ERR_BAD_STATE;
    switch (event_type) {
    case 0x0007: stats->epoch_advances++; break;      /* EPOCH_ADVANCE */
    case 0x0008: stats->compactions_run++; break;      /* COMPACT_TRIGGER */
    case 0x0003: stats->channels_created++; break;     /* CHANNEL_CREATE */
    case 0x000A: stats->channels_closed++; break;      /* CHANNEL_CLOSE */
    case 0x0005: stats->streams_opened++; break;       /* STREAM_OPEN */
    case 0x000F: stats->migrations_started++; break;   /* MIGRATE_PREPARE */
    case 0x0011: stats->migrations_completed++; break; /* MIGRATE_COMMIT */
    default: break;
    }
    return GMX_OK;
}

gmx_status_t gmx_session_stats_record_crc_failure(gmx_session_stats_t *stats)
{
    if (!stats) return GMX_ERR_BAD_STATE;
    stats->crc_failures++;
    return GMX_OK;
}

gmx_status_t gmx_session_stats_record_seq_failure(gmx_session_stats_t *stats)
{
    if (!stats) return GMX_ERR_BAD_STATE;
    stats->sequence_failures++;
    return GMX_OK;
}

float gmx_session_stats_accept_rate(const gmx_session_stats_t *stats)
{
    if (!stats) return 0.0f;
    uint32_t total = stats->frames_processed + stats->frames_rejected;
    if (total == 0) return 0.0f;
    return (float)stats->frames_processed / (float)total;
}

uint32_t gmx_session_stats_total_frames(const gmx_session_stats_t *stats)
{
    if (!stats) return 0;
    return stats->frames_processed + stats->frames_rejected;
}

gmx_status_t gmx_session_stats_merge(gmx_session_stats_t *dst,
                                        const gmx_session_stats_t *src)
{
    if (!dst || !src) return GMX_ERR_BAD_STATE;
    dst->frames_processed += src->frames_processed;
    dst->frames_rejected += src->frames_rejected;
    dst->bytes_processed += src->bytes_processed;
    dst->epoch_advances += src->epoch_advances;
    dst->compactions_run += src->compactions_run;
    dst->channels_created += src->channels_created;
    dst->channels_closed += src->channels_closed;
    dst->streams_opened += src->streams_opened;
    dst->streams_closed += src->streams_closed;
    dst->migrations_started += src->migrations_started;
    dst->migrations_completed += src->migrations_completed;
    dst->crc_failures += src->crc_failures;
    dst->sequence_failures += src->sequence_failures;
    uint32_t total = dst->frames_processed;
    if (total > 0) {
        dst->avg_frame_size = (float)dst->bytes_processed / (float)total;
    }
    return GMX_OK;
}
