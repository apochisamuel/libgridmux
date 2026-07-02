/*
 * libgridmux - CRC-32C computation
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_CRC_H
#define GMX_CRC_H

#include <stdint.h>
#include <stddef.h>

uint32_t gmx_crc32c(uint32_t crc, const uint8_t *data, size_t len);
uint32_t gmx_crc32c_finalize(uint32_t crc);

#endif /* GMX_CRC_H */
