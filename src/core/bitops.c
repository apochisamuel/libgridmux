/*
 * libgridmux - Byte-level and bit-level utilities
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Compact helper routines used by the wire protocol encoder/decoder
 * and internal subsystems for bit field manipulation, byte swapping,
 * alignment arithmetic, and buffer operations.
 */

#include "gmx/types.h"
#include <string.h>

/* Population count (number of set bits) */
uint32_t gmx_popcount32(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (x * 0x01010101u) >> 24;
}

uint32_t gmx_popcount64(uint64_t x)
{
    uint32_t lo = gmx_popcount32((uint32_t)(x & 0xFFFFFFFF));
    uint32_t hi = gmx_popcount32((uint32_t)(x >> 32));
    return lo + hi;
}

/* Count leading zeros */
uint32_t gmx_clz32(uint32_t x)
{
    if (x == 0) return 32;
    uint32_t n = 0;
    if (x <= 0x0000FFFFu) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFFu) { n +=  8; x <<=  8; }
    if (x <= 0x0FFFFFFFu) { n +=  4; x <<=  4; }
    if (x <= 0x3FFFFFFFu) { n +=  2; x <<=  2; }
    if (x <= 0x7FFFFFFFu) { n +=  1; }
    return n;
}

/* Count trailing zeros */
uint32_t gmx_ctz32(uint32_t x)
{
    if (x == 0) return 32;
    uint32_t n = 0;
    if ((x & 0x0000FFFFu) == 0) { n += 16; x >>= 16; }
    if ((x & 0x000000FFu) == 0) { n +=  8; x >>=  8; }
    if ((x & 0x0000000Fu) == 0) { n +=  4; x >>=  4; }
    if ((x & 0x00000003u) == 0) { n +=  2; x >>=  2; }
    if ((x & 0x00000001u) == 0) { n +=  1; }
    return n;
}

/* Byte swap */
uint16_t gmx_bswap16(uint16_t x)
{
    return (x >> 8) | (x << 8);
}

uint32_t gmx_bswap32(uint32_t x)
{
    x = ((x & 0xFF00FF00u) >> 8) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}

uint64_t gmx_bswap64(uint64_t x)
{
    x = ((x & 0xFF00FF00FF00FF00ULL) >> 8) |
        ((x & 0x00FF00FF00FF00FFULL) << 8);
    x = ((x & 0xFFFF0000FFFF0000ULL) >> 16) |
        ((x & 0x0000FFFF0000FFFFULL) << 16);
    return (x >> 32) | (x << 32);
}

/* Round up to power of 2 */
uint32_t gmx_next_pow2(uint32_t x)
{
    if (x == 0) return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

/* Safe alignment */
size_t gmx_align_up(size_t n, size_t alignment)
{
    if (alignment == 0) return n;
    return (n + alignment - 1) & ~(alignment - 1);
}

bool gmx_is_aligned(size_t n, size_t alignment)
{
    if (alignment == 0) return true;
    return (n & (alignment - 1)) == 0;
}

/* Buffer operations */

/*
 * Compare two byte buffers with constant-time comparison
 * to avoid timing side channels in authentication checks.
 */
bool gmx_memequal_ct(const void *a, const void *b, size_t len)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= pa[i] ^ pb[i];
    }
    return diff == 0;
}

/*
 * Secure memory clear that won't be optimized away.
 */
void gmx_memzero(void *ptr, size_t len)
{
    volatile uint8_t *vp = (volatile uint8_t *)ptr;
    while (len--) {
        *vp++ = 0;
    }
}

/*
 * XOR two byte buffers into dst.
 */
void gmx_memxor(uint8_t *dst, const uint8_t *a, const uint8_t *b, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        dst[i] = a[i] ^ b[i];
    }
}

/*
 * Simple non-cryptographic hash for internal use.
 * FNV-1a 32-bit.
 */
uint32_t gmx_fnv1a_32(const uint8_t *data, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

/*
 * FNV-1a 64-bit variant.
 */
uint64_t gmx_fnv1a_64(const uint8_t *data, size_t len)
{
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/*
 * Encode a variable-length integer (LEB128).
 * Returns bytes written.
 */
size_t gmx_encode_varint(uint8_t *dst, size_t cap, uint64_t value)
{
    size_t n = 0;
    do {
        if (n >= cap) return 0;
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value != 0) byte |= 0x80;
        dst[n++] = byte;
    } while (value != 0);
    return n;
}

/*
 * Decode a variable-length integer (LEB128).
 * Returns bytes consumed, or 0 on error.
 */
size_t gmx_decode_varint(const uint8_t *src, size_t len,
                          uint64_t *out_value)
{
    if (!src || !out_value || len == 0) return 0;

    uint64_t result = 0;
    uint32_t shift = 0;
    size_t n = 0;

    do {
        if (n >= len || shift >= 64) return 0;
        uint8_t byte = src[n];
        result |= (uint64_t)(byte & 0x7F) << shift;
        shift += 7;
        n++;
        if (!(byte & 0x80)) break;
    } while (1);

    *out_value = result;
    return n;
}

/*
 * Compute a Murmur3-like mixing function for hash seed derivation.
 */
uint32_t gmx_mix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

uint64_t gmx_mix64(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return h;
}
