/*
 * libgridmux - Multiplexed binary transport for service mesh data planes
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_TYPES_H
#define GMX_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define GMX_MAGIC           0x474D5831u  /* "GMX1" */
#define GMX_VERSION_MAJOR   1
#define GMX_VERSION_MINOR   0
#define GMX_VERSION         ((GMX_VERSION_MAJOR << 8) | GMX_VERSION_MINOR)

#define GMX_MAX_SESSIONS    16
#define GMX_MAX_CHANNELS    64
#define GMX_MAX_STREAMS     128
#define GMX_MAX_ROUTES      64
#define GMX_MAX_LEASES      128
#define GMX_MAX_FRAME_COUNT 64
#define GMX_MAX_SUBRECORDS  16
#define GMX_MAX_PAYLOAD     4096
#define GMX_MAX_FRAME_LEN   8192

#define GMX_EPOCH_COMPACT_THRESHOLD  3
#define GMX_ROUTE_ENTRY_MAX          256
#define GMX_REGISTRY_BUCKETS         128

/* Session header: on-wire layout */
typedef struct gmx_session_header {
    uint32_t magic;
    uint16_t version;
    uint32_t session_id;
    uint32_t epoch;
    uint16_t frame_count;
    uint32_t header_crc;
} __attribute__((packed)) gmx_session_header_t;

#define GMX_SESSION_HEADER_SIZE  18  /* bytes before CRC + 4 CRC = 22 total? */
/* Actually: 4+2+4+4+2 = 16 before CRC, +4 CRC = 20 total */
#define GMX_SESSION_HEADER_WIRE_SIZE 20

/* Frame types */
typedef enum gmx_frame_type {
    GMX_FRAME_SESSION_INIT      = 0x0001,
    GMX_FRAME_ROUTE_ADVERTISE   = 0x0002,
    GMX_FRAME_CHANNEL_CREATE    = 0x0003,
    GMX_FRAME_CHANNEL_BIND      = 0x0004,
    GMX_FRAME_STREAM_OPEN       = 0x0005,
    GMX_FRAME_DATA_WRITE        = 0x0006,
    GMX_FRAME_EPOCH_ADVANCE     = 0x0007,
    GMX_FRAME_COMPACT_TRIGGER   = 0x0008,
    GMX_FRAME_DATA_FORWARD      = 0x0009,
    GMX_FRAME_CHANNEL_CLOSE     = 0x000A,
    GMX_FRAME_STREAM_CLOSE      = 0x000B,
    GMX_FRAME_LEASE_ACQUIRE     = 0x000C,
    GMX_FRAME_LEASE_RELEASE     = 0x000D,
    GMX_FRAME_MIGRATE_PREPARE   = 0x000E,
    GMX_FRAME_MIGRATE_TRANSFER  = 0x000F,
    GMX_FRAME_MIGRATE_COMMIT    = 0x0010,
    GMX_FRAME_MIGRATE_ROLLBACK  = 0x0011,
    GMX_FRAME_QUOTA_ALLOC       = 0x0012,
    GMX_FRAME_HEARTBEAT         = 0x0013,
    GMX_FRAME_JOURNAL_APPEND    = 0x0014,
    GMX_FRAME_JOURNAL_REPLAY    = 0x0015,
    GMX_FRAME_NONCE_VERIFY      = 0x0016,
    GMX_FRAME_CREDIT_GRANT      = 0x0017,
    GMX_FRAME_CREDIT_FLUSH      = 0x0018,
    GMX_FRAME_ROUTE_CASCADE     = 0x0019,
    GMX_FRAME_CHECKPOINT_WRITE  = 0x001A,
    GMX_FRAME_CHECKPOINT_REPLAY = 0x001B,
    GMX_FRAME_LEASE_EXTEND      = 0x001C,
    GMX_FRAME_TYPE_MAX
} gmx_frame_type_t;

/* Frame header: on-wire layout */
typedef struct gmx_frame_header {
    uint32_t length;
    uint16_t frame_type;
    uint32_t sequence;
    uint32_t session_id;
    uint32_t channel_id;
    uint32_t route_id;
    uint32_t owner_token;
    uint16_t flags;
    uint16_t subrecord_count;
    uint32_t payload_crc;
} __attribute__((packed)) gmx_frame_header_t;

/* 4+2+4+4+4+4+4+2+2+4 = 34 bytes */
#define GMX_FRAME_HEADER_WIRE_SIZE 34

/* Subrecord kinds */
typedef enum gmx_subrecord_kind {
    GMX_SUB_CONFIG       = 0x01,
    GMX_SUB_ROUTE_SPEC   = 0x02,
    GMX_SUB_CHANNEL_SPEC = 0x03,
    GMX_SUB_BIND_SPEC    = 0x04,
    GMX_SUB_STREAM_SPEC  = 0x05,
    GMX_SUB_DATA_PAYLOAD = 0x06,
    GMX_SUB_EPOCH_SPEC   = 0x07,
    GMX_SUB_COMPACT_SPEC = 0x08,
    GMX_SUB_FORWARD_SPEC = 0x09,
    GMX_SUB_LEASE_SPEC   = 0x0A,
    GMX_SUB_MIGRATE_SPEC = 0x0B,
    GMX_SUB_QUOTA_SPEC   = 0x0C,
    GMX_SUB_JOURNAL_SPEC = 0x0D,
    GMX_SUB_NONCE_SPEC   = 0x0E,
    GMX_SUB_CREDIT_SPEC  = 0x0F,
    GMX_SUB_CHECKPOINT   = 0x10,
    GMX_SUB_KIND_MAX
} gmx_subrecord_kind_t;

/* Subrecord header: on-wire layout */
typedef struct gmx_subrecord_header {
    uint16_t kind;
    uint16_t length;      /* total subrecord length including this header */
    uint32_t local_epoch;
    uint32_t generation;
    uint32_t lease_token;
} __attribute__((packed)) gmx_subrecord_header_t;

/* 2+2+4+4+4 = 16 bytes */
#define GMX_SUBRECORD_HEADER_WIRE_SIZE 16

/* Parsed subrecord (in-memory) */
typedef struct gmx_subrecord {
    gmx_subrecord_kind_t kind;
    uint16_t             total_length;
    uint32_t             local_epoch;
    uint32_t             generation;
    uint32_t             lease_token;
    const uint8_t       *payload;
    uint16_t             payload_length;
} gmx_subrecord_t;

/* Parsed frame (in-memory) */
typedef struct gmx_frame {
    gmx_frame_header_t  header;
    gmx_subrecord_t     subrecords[GMX_MAX_SUBRECORDS];
    uint16_t            subrecord_count;
    const uint8_t      *raw;
    size_t              raw_length;
} gmx_frame_t;

/* Forward declarations */
typedef struct gmx_session      gmx_session_t;
typedef struct gmx_channel      gmx_channel_t;
typedef struct gmx_stream       gmx_stream_t;
typedef struct gmx_route_entry  gmx_route_entry_t;
typedef struct gmx_route_table  gmx_route_table_t;
typedef struct gmx_registry     gmx_registry_t;
typedef struct gmx_compact_ctx  gmx_compact_ctx_t;
typedef struct gmx_slab_pool    gmx_slab_pool_t;
typedef struct gmx_lease        gmx_lease_t;
typedef struct gmx_lease_table  gmx_lease_table_t;

#endif /* GMX_TYPES_H */
