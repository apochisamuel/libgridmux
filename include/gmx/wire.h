/*
 * libgridmux - Wire protocol encoding and decoding
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GMX_WIRE_H
#define GMX_WIRE_H

#include "types.h"
#include "error.h"

/* Decode session header from raw bytes. Returns GMX_OK or error. */
gmx_status_t gmx_wire_decode_session_header(const uint8_t *data, size_t len,
                                             gmx_session_header_t *out);

/* Decode a single frame from raw bytes. Returns GMX_OK or error.
 * consumed is set to the number of bytes consumed. */
gmx_status_t gmx_wire_decode_frame(const uint8_t *data, size_t len,
                                    gmx_frame_t *out, size_t *consumed);

/* Decode subrecords within a frame body. */
gmx_status_t gmx_wire_decode_subrecords(const uint8_t *data, size_t len,
                                         gmx_subrecord_t *out,
                                         uint16_t max_count,
                                         uint16_t *actual_count);

/* Validate frame CRC against payload. */
bool gmx_wire_validate_frame_crc(const gmx_frame_t *frame);

/* Read little-endian integers from buffer. */
uint16_t gmx_wire_read_u16(const uint8_t *p);
uint32_t gmx_wire_read_u32(const uint8_t *p);

/* Write little-endian integers to buffer. */
void gmx_wire_write_u16(uint8_t *p, uint16_t val);
void gmx_wire_write_u32(uint8_t *p, uint32_t val);

/* Resolve next-hop for data forwarding through stream's channel route. */
gmx_status_t gmx_wire_resolve_next_hop(const gmx_stream_t *stream,
                                        uint8_t *dst, size_t dst_cap,
                                        size_t *out_len);

#endif /* GMX_WIRE_H */
