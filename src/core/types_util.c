/*
 * libgridmux - Type conversion and validation utilities
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Provides safe arithmetic, range validation, overflow detection,
 * and type-safe conversion helpers used throughout the library.
 */
#include "gmx/types.h"
#include "gmx/error.h"
#include <limits.h>
#include <string.h>

/* Safe unsigned addition with overflow detection */
gmx_status_t gmx_safe_add_u32(uint32_t a, uint32_t b, uint32_t *result)
{
    if (!result) return GMX_ERR_BAD_STATE;
    if (a > UINT32_MAX - b) return GMX_ERR_OVERFLOW;
    *result = a + b;
    return GMX_OK;
}

gmx_status_t gmx_safe_add_u64(uint64_t a, uint64_t b, uint64_t *result)
{
    if (!result) return GMX_ERR_BAD_STATE;
    if (a > UINT64_MAX - b) return GMX_ERR_OVERFLOW;
    *result = a + b;
    return GMX_OK;
}

/* Safe unsigned multiplication with overflow detection */
gmx_status_t gmx_safe_mul_u32(uint32_t a, uint32_t b, uint32_t *result)
{
    if (!result) return GMX_ERR_BAD_STATE;
    if (b != 0 && a > UINT32_MAX / b) return GMX_ERR_OVERFLOW;
    *result = a * b;
    return GMX_OK;
}

gmx_status_t gmx_safe_mul_u64(uint64_t a, uint64_t b, uint64_t *result)
{
    if (!result) return GMX_ERR_BAD_STATE;
    if (b != 0 && a > UINT64_MAX / b) return GMX_ERR_OVERFLOW;
    *result = a * b;
    return GMX_OK;
}

/* Safe subtraction (clamped to zero) */
uint32_t gmx_saturating_sub_u32(uint32_t a, uint32_t b)
{
    return a > b ? a - b : 0;
}

uint64_t gmx_saturating_sub_u64(uint64_t a, uint64_t b)
{
    return a > b ? a - b : 0;
}

/* Safe addition (clamped to max) */
uint32_t gmx_saturating_add_u32(uint32_t a, uint32_t b)
{
    uint32_t result = a + b;
    return result >= a ? result : UINT32_MAX;
}

/* Range validation */
bool gmx_in_range_u32(uint32_t value, uint32_t min, uint32_t max)
{
    return value >= min && value <= max;
}

bool gmx_in_range_size(size_t value, size_t min, size_t max)
{
    return value >= min && value <= max;
}

/* Size validation for allocation */
bool gmx_valid_alloc_size(size_t count, size_t element_size)
{
    if (count == 0 || element_size == 0) return false;
    return count <= SIZE_MAX / element_size;
}

/* Frame type validation */
bool gmx_is_valid_frame_type(uint16_t frame_type)
{
    return frame_type >= GMX_FRAME_SESSION_INIT &&
           frame_type <= GMX_FRAME_CHECKPOINT_WRITE;
}

/* Subrecord kind validation */
bool gmx_is_valid_subrecord_kind(uint16_t kind)
{
    return kind >= GMX_SUB_CONFIG && kind <= GMX_SUB_FORWARD_SPEC;
}

/* Status code classification */
bool gmx_status_is_error(gmx_status_t status)
{
    return status != GMX_OK;
}

bool gmx_status_is_transient(gmx_status_t status)
{
    switch (status) {
    case GMX_ERR_ALLOC:
    case GMX_ERR_COMPACT_BUSY:
        return true;
    default:
        return false;
    }
}

bool gmx_status_is_permanent(gmx_status_t status)
{
    switch (status) {
    case GMX_ERR_INVALID_MAGIC:
    
    case GMX_ERR_CRC_MISMATCH:
        return true;
    default:
        return false;
    }
}

/* Epoch comparison (handles wraparound) */
bool gmx_epoch_before(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

bool gmx_epoch_after(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

uint32_t gmx_epoch_distance(uint32_t from, uint32_t to)
{
    return to - from;
}

/* ID hashing for table distribution */
uint32_t gmx_id_hash(uint32_t id, uint32_t table_size)
{
    /* FNV-1a inspired single-u32 hash */
    uint32_t h = id;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h % table_size;
}

/* Sequence number comparison (handles wraparound) */
bool gmx_seq_before(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

bool gmx_seq_after(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

int32_t gmx_seq_diff(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b);
}

/* Token generation for authentication */
uint32_t gmx_generate_token(uint32_t seed, uint32_t session_id, uint32_t epoch)
{
    uint32_t h = seed;
    h ^= session_id;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= epoch;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

/* Metric clamping for route computation */
uint8_t gmx_clamp_metric(uint32_t value)
{
    return value > 255 ? 255 : (uint8_t)value;
}

uint32_t gmx_weighted_metric(uint8_t metric, uint32_t weight)
{
    return (uint32_t)metric * weight;
}
