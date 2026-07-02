/*
 * libgridmux - Session lifecycle unit tests
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/session.h"
#include "gmx/channel.h"
#include "gmx/stream.h"
#include "gmx/route.h"
#include "gmx/registry.h"
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

static void test_session_create_destroy(void)
{
    gmx_session_t *sess = gmx_session_create(1);
    assert(sess != NULL);
    assert(sess->session_id == 1);
    assert(sess->active == false);
    assert(sess->epoch == 0);
    gmx_session_destroy(sess);
}

static void test_session_find_channel_empty(void)
{
    gmx_session_t *sess = gmx_session_create(1);
    assert(gmx_session_find_channel(sess, 1) == NULL);
    gmx_session_destroy(sess);
}

static void test_session_find_stream_empty(void)
{
    gmx_session_t *sess = gmx_session_create(1);
    assert(gmx_session_find_stream(sess, 1) == NULL);
    gmx_session_destroy(sess);
}

static void test_session_advance_epoch(void)
{
    gmx_session_t *sess = gmx_session_create(1);
    sess->active = true;
    gmx_status_t rc = gmx_session_advance_epoch(sess, 1);
    assert(rc == GMX_OK);
    assert(sess->epoch == 1);

    rc = gmx_session_advance_epoch(sess, 0);
    assert(rc == GMX_ERR_BAD_EPOCH);
    assert(sess->epoch == 1);

    gmx_session_destroy(sess);
}

static void test_session_advance_epoch_monotonic(void)
{
    gmx_session_t *sess = gmx_session_create(1);
    sess->active = true;
    assert(gmx_session_advance_epoch(sess, 1) == GMX_OK);
    assert(gmx_session_advance_epoch(sess, 2) == GMX_OK);
    assert(gmx_session_advance_epoch(sess, 2) == GMX_ERR_BAD_EPOCH);
    assert(gmx_session_advance_epoch(sess, 3) == GMX_OK);
    assert(sess->epoch == 3);
    gmx_session_destroy(sess);
}

static void test_channel_create_destroy(void)
{
    gmx_channel_t *ch = gmx_channel_create(1, 1, 0xBEEF);
    assert(ch != NULL);
    assert(ch->channel_id == 1);
    assert(ch->active == true);
    assert(ch->credit_available == GMX_CHANNEL_CREDIT_DEFAULT);
    gmx_channel_destroy(ch);
}

static void test_channel_credit_consume(void)
{
    gmx_channel_t *ch = gmx_channel_create(1, 1, 0);
    assert(ch->credit_available == GMX_CHANNEL_CREDIT_DEFAULT);

    gmx_status_t rc = gmx_channel_consume_credit(ch);
    assert(rc == GMX_OK);
    assert(ch->credit_available == GMX_CHANNEL_CREDIT_DEFAULT - 1);

    gmx_channel_destroy(ch);
}

static void test_stream_create_destroy(void)
{
    gmx_channel_t *ch = gmx_channel_create(1, 1, 0);
    gmx_stream_t *s = gmx_stream_create(1, 1, ch);
    assert(s != NULL);
    assert(s->stream_id == 1);
    assert(s->channel_id == 1);
    assert(s->bytes_written == 0);
    gmx_stream_destroy(s);
    gmx_channel_destroy(ch);
}

static void test_stream_write(void)
{
    gmx_channel_t *ch = gmx_channel_create(1, 1, 0);
    gmx_stream_t *s = gmx_stream_create(1, 1, ch);

    uint8_t data[] = {1, 2, 3, 4};
    gmx_status_t rc = gmx_stream_write(s, data, sizeof(data));
    assert(rc == GMX_OK);
    assert(s->bytes_written == 4);

    gmx_stream_destroy(s);
    gmx_channel_destroy(ch);
}

static void test_route_create_lookup(void)
{
    gmx_route_table_t table;
    gmx_route_table_init(&table, 16);

    uint8_t seg[] = {10, 0, 1, 1, 80, 0, 0xAA, 0xBB};
    gmx_route_entry_t *entry = gmx_route_entry_create(&table, 1, 1, 0,
                                                        seg, sizeof(seg));
    assert(entry != NULL);
    assert(entry->route_id == 1);

    gmx_route_entry_t *found = gmx_route_table_lookup(&table, 1);
    assert(found == entry);

    assert(gmx_route_table_lookup(&table, 99) == NULL);

    gmx_route_table_destroy(&table);
}

int main(void)
{
    printf("Session lifecycle tests:\n");
    TEST(session_create_destroy);
    TEST(session_find_channel_empty);
    TEST(session_find_stream_empty);
    TEST(session_advance_epoch);
    TEST(session_advance_epoch_monotonic);
    TEST(channel_create_destroy);
    TEST(channel_credit_consume);
    TEST(stream_create_destroy);
    TEST(stream_write);
    TEST(route_create_lookup);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
