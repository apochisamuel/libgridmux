/*
 * libgridmux - Compaction unit tests
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 */
#include "gmx/route.h"
#include "gmx/compact.h"
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

static void test_compact_ctx_init(void)
{
    gmx_compact_ctx_t ctx;
    gmx_compact_ctx_init(&ctx);
    assert(ctx.active == false);
    assert(ctx.watermark_epoch == 0);
}

static void test_compact_empty_table(void)
{
    gmx_route_table_t table;
    gmx_route_table_init(&table, 16);
    gmx_compact_ctx_t ctx;
    gmx_compact_ctx_init(&ctx);

    gmx_status_t rc = gmx_compact_route_table(&ctx, &table, 5, 2);
    assert(rc == GMX_OK);
    assert(ctx.entries_freed == 0);
    assert(ctx.entries_scanned == 0);

    gmx_route_table_destroy(&table);
}

static void test_compact_retains_recent(void)
{
    gmx_route_table_t table;
    gmx_route_table_init(&table, 16);
    gmx_compact_ctx_t ctx;
    gmx_compact_ctx_init(&ctx);

    /* Create entry at epoch 3 */
    gmx_route_entry_create(&table, 1, 1, 3, NULL, 0);
    assert(table.count == 1);

    /* Compact at epoch 5 with threshold 2 → watermark = 3 */
    /* epoch_created 3 < watermark 3 is FALSE, so should NOT be freed */
    gmx_compact_route_table(&ctx, &table, 5, 2);
    assert(ctx.entries_freed == 0);
    assert(table.count == 1);

    gmx_route_table_destroy(&table);
}

static void test_compact_frees_old(void)
{
    gmx_route_table_t table;
    gmx_route_table_init(&table, 16);
    gmx_compact_ctx_t ctx;
    gmx_compact_ctx_init(&ctx);

    /* Create entry at epoch 0 */
    gmx_route_entry_create(&table, 1, 1, 0, NULL, 0);
    assert(table.count == 1);

    /* Compact at epoch 5 with threshold 2 → watermark = 3 */
    /* epoch_created 0 < watermark 3 → should be freed */
    gmx_compact_route_table(&ctx, &table, 5, 2);
    assert(ctx.entries_freed == 1);
    assert(table.count == 0);

    gmx_route_table_destroy(&table);
}

static void test_compact_multiple_entries(void)
{
    gmx_route_table_t table;
    gmx_route_table_init(&table, 16);
    gmx_compact_ctx_t ctx;
    gmx_compact_ctx_init(&ctx);

    gmx_route_entry_create(&table, 1, 1, 0, NULL, 0);  /* old */
    gmx_route_entry_create(&table, 2, 2, 1, NULL, 0);  /* old */
    gmx_route_entry_create(&table, 3, 3, 4, NULL, 0);  /* recent */
    assert(table.count == 3);

    gmx_compact_route_table(&ctx, &table, 5, 2);
    assert(ctx.entries_freed == 2);  /* entries at epoch 0 and 1 freed */
    assert(table.count == 1);       /* only epoch 4 remains */

    gmx_route_table_destroy(&table);
}

static void test_compact_with_segment_data(void)
{
    gmx_route_table_t table;
    gmx_route_table_init(&table, 16);
    gmx_compact_ctx_t ctx;
    gmx_compact_ctx_init(&ctx);

    uint8_t seg[] = {10, 0, 0, 1, 80, 0, 0xAA, 0xBB};
    gmx_route_entry_create(&table, 1, 1, 0, seg, sizeof(seg));

    gmx_compact_route_table(&ctx, &table, 5, 2);
    assert(ctx.entries_freed == 1);
    assert(table.count == 0);

    gmx_route_table_destroy(&table);
}

static void test_compact_busy_check(void)
{
    gmx_route_table_t table;
    gmx_route_table_init(&table, 16);
    gmx_compact_ctx_t ctx;
    gmx_compact_ctx_init(&ctx);
    ctx.active = true;

    gmx_status_t rc = gmx_compact_route_table(&ctx, &table, 5, 2);
    assert(rc == GMX_ERR_COMPACT_BUSY);

    ctx.active = false;
    gmx_route_table_destroy(&table);
}

int main(void)
{
    printf("Compaction tests:\n");
    TEST(compact_ctx_init);
    TEST(compact_empty_table);
    TEST(compact_retains_recent);
    TEST(compact_frees_old);
    TEST(compact_multiple_entries);
    TEST(compact_with_segment_data);
    TEST(compact_busy_check);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
