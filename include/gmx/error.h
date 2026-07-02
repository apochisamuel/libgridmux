/*
 * libgridmux - Error codes and error handling
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_ERROR_H
#define GMX_ERROR_H

typedef enum gmx_status {
    GMX_OK                  =  0,
    GMX_ERR_INVALID_MAGIC   = -1,
    GMX_ERR_INVALID_VERSION = -2,
    GMX_ERR_CRC_MISMATCH    = -3,
    GMX_ERR_TRUNCATED       = -4,
    GMX_ERR_SESSION_FULL    = -5,
    GMX_ERR_CHANNEL_FULL    = -6,
    GMX_ERR_STREAM_FULL     = -7,
    GMX_ERR_ROUTE_FULL      = -8,
    GMX_ERR_NOT_FOUND       = -9,
    GMX_ERR_BAD_SEQUENCE    = -10,
    GMX_ERR_BAD_SESSION_ID  = -11,
    GMX_ERR_BAD_EPOCH       = -12,
    GMX_ERR_BAD_OWNER       = -13,
    GMX_ERR_BAD_FRAME_TYPE  = -14,
    GMX_ERR_BAD_SUBRECORD   = -15,
    GMX_ERR_BAD_LENGTH      = -16,
    GMX_ERR_BAD_STATE       = -17,
    GMX_ERR_ALLOC           = -18,
    GMX_ERR_REGISTRY_FULL   = -19,
    GMX_ERR_BAD_GENERATION  = -20,
    GMX_ERR_BAD_LEASE       = -21,
    GMX_ERR_BAD_CHANNEL     = -22,
    GMX_ERR_BAD_ROUTE       = -23,
    GMX_ERR_BAD_STREAM      = -24,
    GMX_ERR_NOT_ACTIVE      = -25,
    GMX_ERR_DUPLICATE       = -26,
    GMX_ERR_OVERFLOW        = -27,
    GMX_ERR_COMPACT_BUSY    = -28,
} gmx_status_t;

const char *gmx_status_str(gmx_status_t status);

#endif /* GMX_ERROR_H */
