/*
 * libgridmux - Error code string representations
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "gmx/error.h"

const char *gmx_status_str(gmx_status_t status)
{
    switch (status) {
    case GMX_OK:                  return "ok";
    case GMX_ERR_INVALID_MAGIC:   return "invalid magic";
    case GMX_ERR_INVALID_VERSION: return "invalid version";
    case GMX_ERR_CRC_MISMATCH:    return "crc mismatch";
    case GMX_ERR_TRUNCATED:       return "truncated input";
    case GMX_ERR_SESSION_FULL:    return "session table full";
    case GMX_ERR_CHANNEL_FULL:    return "channel table full";
    case GMX_ERR_STREAM_FULL:     return "stream table full";
    case GMX_ERR_ROUTE_FULL:      return "route table full";
    case GMX_ERR_NOT_FOUND:       return "not found";
    case GMX_ERR_BAD_SEQUENCE:    return "bad sequence number";
    case GMX_ERR_BAD_SESSION_ID:  return "bad session id";
    case GMX_ERR_BAD_EPOCH:       return "bad epoch";
    case GMX_ERR_BAD_OWNER:       return "bad owner token";
    case GMX_ERR_BAD_FRAME_TYPE:  return "bad frame type";
    case GMX_ERR_BAD_SUBRECORD:   return "bad subrecord";
    case GMX_ERR_BAD_LENGTH:      return "bad length";
    case GMX_ERR_BAD_STATE:       return "bad state";
    case GMX_ERR_ALLOC:           return "allocation failed";
    case GMX_ERR_REGISTRY_FULL:   return "registry full";
    case GMX_ERR_BAD_GENERATION:  return "bad generation";
    case GMX_ERR_BAD_LEASE:       return "bad lease";
    case GMX_ERR_BAD_CHANNEL:     return "bad channel";
    case GMX_ERR_BAD_ROUTE:       return "bad route";
    case GMX_ERR_BAD_STREAM:      return "bad stream";
    case GMX_ERR_NOT_ACTIVE:      return "not active";
    case GMX_ERR_DUPLICATE:       return "duplicate entry";
    case GMX_ERR_OVERFLOW:        return "overflow";
    case GMX_ERR_COMPACT_BUSY:    return "compaction in progress";
    }
    return "unknown error";
}
