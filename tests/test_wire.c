/*
 * libgridmux - Wire protocol unit tests
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/wire.h"
#include "gmx/crc.h"
#include "gmx/types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-50s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("[PASS]\n"); \
} while(0)

static void test_read_write_u16(void)
{
    uint8_t buf[2];
    gmx_wire_write_u16(buf, 0x1234);
    assert(gmx_wire_read_u16(buf) == 0x1234);
    gmx_wire_write_u16(buf, 0);
    assert(gmx_wire_read_u16(buf) == 0);
    gmx_wire_write_u16(buf, 0xFFFF);
    assert(gmx_wire_read_u16(buf) == 0xFFFF);
}

static void test_read_write_u32(void)
{
    uint8_t buf[4];
    gmx_wire_write_u32(buf, 0xDEADBEEF);
    assert(gmx_wire_read_u32(buf) == 0xDEADBEEF);
    gmx_wire_write_u32(buf, 0);
    assert(gmx_wire_read_u32(buf) == 0);
    gmx_wire_write_u32(buf, 1);
    assert(gmx_wire_read_u32(buf) == 1);
}

static void test_session_header_decode(void)
{
    uint8_t hdr[GMX_SESSION_HEADER_WIRE_SIZE];
    memset(hdr, 0, sizeof(hdr));

    gmx_wire_write_u32(hdr, GMX_MAGIC);
    gmx_wire_write_u16(hdr + 4, GMX_VERSION);
    gmx_wire_write_u32(hdr + 6, 42);    /* session_id */
    gmx_wire_write_u32(hdr + 10, 0);    /* epoch */
    gmx_wire_write_u16(hdr + 14, 1);    /* frame_count */
    uint32_t crc = gmx_crc32c(0, hdr, 16);
    gmx_wire_write_u32(hdr + 16, crc);

    gmx_session_header_t out;
    gmx_status_t rc = gmx_wire_decode_session_header(hdr, sizeof(hdr), &out);
    assert(rc == GMX_OK);
    assert(out.magic == GMX_MAGIC);
    assert(out.version == GMX_VERSION);
    assert(out.session_id == 42);
    assert(out.epoch == 0);
    assert(out.frame_count == 1);
}

static void test_session_header_bad_magic(void)
{
    uint8_t hdr[GMX_SESSION_HEADER_WIRE_SIZE];
    memset(hdr, 0, sizeof(hdr));
    gmx_wire_write_u32(hdr, 0x12345678);  /* wrong magic */

    gmx_session_header_t out;
    gmx_status_t rc = gmx_wire_decode_session_header(hdr, sizeof(hdr), &out);
    assert(rc == GMX_ERR_INVALID_MAGIC);
}

static void test_session_header_truncated(void)
{
    uint8_t hdr[10];
    memset(hdr, 0, sizeof(hdr));

    gmx_session_header_t out;
    gmx_status_t rc = gmx_wire_decode_session_header(hdr, sizeof(hdr), &out);
    assert(rc == GMX_ERR_TRUNCATED);
}

static void test_session_header_bad_crc(void)
{
    uint8_t hdr[GMX_SESSION_HEADER_WIRE_SIZE];
    memset(hdr, 0, sizeof(hdr));
    gmx_wire_write_u32(hdr, GMX_MAGIC);
    gmx_wire_write_u16(hdr + 4, GMX_VERSION);
    gmx_wire_write_u32(hdr + 6, 1);
    gmx_wire_write_u32(hdr + 10, 0);
    gmx_wire_write_u16(hdr + 14, 1);
    gmx_wire_write_u32(hdr + 16, 0xDEADDEAD);  /* wrong CRC */

    gmx_session_header_t out;
    gmx_status_t rc = gmx_wire_decode_session_header(hdr, sizeof(hdr), &out);
    assert(rc == GMX_ERR_CRC_MISMATCH);
}

static void test_crc32c_empty(void)
{
    uint32_t crc = gmx_crc32c(0, (const uint8_t *)"", 0);
    assert(crc == 0);
}

static void test_crc32c_data(void)
{
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint32_t crc1 = gmx_crc32c(0, data, 4);
    uint32_t crc2 = gmx_crc32c(0, data, 4);
    assert(crc1 == crc2);
    assert(crc1 != 0);
}

static void test_crc32c_different_data(void)
{
    const uint8_t data1[] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t data2[] = {0x04, 0x03, 0x02, 0x01};
    uint32_t crc1 = gmx_crc32c(0, data1, 4);
    uint32_t crc2 = gmx_crc32c(0, data2, 4);
    assert(crc1 != crc2);
}

int main(void)
{
    printf("Wire protocol tests:\n");
    TEST(read_write_u16);
    TEST(read_write_u32);
    TEST(session_header_decode);
    TEST(session_header_bad_magic);
    TEST(session_header_truncated);
    TEST(session_header_bad_crc);
    TEST(crc32c_empty);
    TEST(crc32c_data);
    TEST(crc32c_different_data);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
