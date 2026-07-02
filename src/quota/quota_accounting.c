/*
 * libgridmux - Cross-group quota accounting and reconciliation
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Handles quota reconciliation across groups after migrations,
 * epoch transitions, and GC sweeps. Maintains an accounting
 * ledger for audit trail purposes.
 */
#include "gmx/quota.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_ACCOUNTING_LEDGER_SIZE  64

typedef enum gmx_accounting_op {
    GMX_ACCT_CONSUME    = 1,
    GMX_ACCT_RELEASE    = 2,
    GMX_ACCT_TRANSFER   = 3,
    GMX_ACCT_ADJUSTMENT = 4,
    GMX_ACCT_GC_RECLAIM = 5
} gmx_accounting_op_t;

typedef struct gmx_accounting_entry {
    uint32_t           group_id;
    gmx_accounting_op_t op;
    uint32_t           amount;
    uint32_t           epoch;
    uint32_t           balance_after;
    bool               valid;
} gmx_accounting_entry_t;

typedef struct gmx_accounting_ledger {
    gmx_accounting_entry_t entries[GMX_ACCOUNTING_LEDGER_SIZE];
    uint32_t               write_pos;
    uint32_t               count;
    uint32_t               total_consumed;
    uint32_t               total_released;
    uint32_t               total_transferred;
    uint32_t               total_reclaimed;
} gmx_accounting_ledger_t;

gmx_status_t gmx_accounting_init(gmx_accounting_ledger_t *ledger)
{
    if (!ledger) return GMX_ERR_BAD_STATE;
    memset(ledger, 0, sizeof(*ledger));
    return GMX_OK;
}

static gmx_status_t record_entry(gmx_accounting_ledger_t *ledger,
                                   uint32_t group_id,
                                   gmx_accounting_op_t op,
                                   uint32_t amount,
                                   uint32_t epoch,
                                   uint32_t balance_after)
{
    if (!ledger) return GMX_ERR_BAD_STATE;

    uint32_t idx = ledger->write_pos % GMX_ACCOUNTING_LEDGER_SIZE;
    gmx_accounting_entry_t *e = &ledger->entries[idx];
    e->group_id      = group_id;
    e->op            = op;
    e->amount        = amount;
    e->epoch         = epoch;
    e->balance_after = balance_after;
    e->valid         = true;

    ledger->write_pos++;
    if (ledger->count < GMX_ACCOUNTING_LEDGER_SIZE) ledger->count++;

    switch (op) {
    case GMX_ACCT_CONSUME:    ledger->total_consumed += amount; break;
    case GMX_ACCT_RELEASE:    ledger->total_released += amount; break;
    case GMX_ACCT_TRANSFER:   ledger->total_transferred += amount; break;
    case GMX_ACCT_GC_RECLAIM: ledger->total_reclaimed += amount; break;
    default: break;
    }

    return GMX_OK;
}

gmx_status_t gmx_accounting_record_consume(gmx_accounting_ledger_t *ledger,
                                              uint32_t group_id,
                                              uint32_t amount,
                                              uint32_t epoch,
                                              uint32_t balance_after)
{
    return record_entry(ledger, group_id, GMX_ACCT_CONSUME,
                         amount, epoch, balance_after);
}

gmx_status_t gmx_accounting_record_release(gmx_accounting_ledger_t *ledger,
                                              uint32_t group_id,
                                              uint32_t amount,
                                              uint32_t epoch,
                                              uint32_t balance_after)
{
    return record_entry(ledger, group_id, GMX_ACCT_RELEASE,
                         amount, epoch, balance_after);
}

gmx_status_t gmx_accounting_record_transfer(gmx_accounting_ledger_t *ledger,
                                               uint32_t group_id,
                                               uint32_t amount,
                                               uint32_t epoch)
{
    return record_entry(ledger, group_id, GMX_ACCT_TRANSFER,
                         amount, epoch, 0);
}

gmx_status_t gmx_accounting_record_gc(gmx_accounting_ledger_t *ledger,
                                         uint32_t group_id,
                                         uint32_t amount,
                                         uint32_t epoch)
{
    return record_entry(ledger, group_id, GMX_ACCT_GC_RECLAIM,
                         amount, epoch, 0);
}

/*
 * Reconcile quota balances after an epoch transition.
 * Verifies that the sum of all group balances matches the
 * expected total from the ledger.
 */
gmx_status_t gmx_accounting_reconcile(const gmx_accounting_ledger_t *ledger,
                                         const gmx_quota_table_t *qt,
                                         bool *balanced,
                                         int32_t *discrepancy)
{
    if (!ledger || !qt || !balanced || !discrepancy) return GMX_ERR_BAD_STATE;

    /* Sum current balances across all groups */
    uint32_t total_consumed_now = 0;
    for (size_t i = 0; i < qt->capacity; i++) {
        if (qt->buckets[i].active) {
            total_consumed_now += qt->buckets[i].consumed;
        }
    }

    /* Expected: total_consumed - total_released - total_reclaimed */
    int32_t expected = (int32_t)ledger->total_consumed -
                       (int32_t)ledger->total_released -
                       (int32_t)ledger->total_reclaimed;

    *discrepancy = (int32_t)total_consumed_now - expected;
    *balanced = (*discrepancy == 0);

    return GMX_OK;
}

/*
 * Get accounting statistics for a specific group.
 */
gmx_status_t gmx_accounting_group_stats(const gmx_accounting_ledger_t *ledger,
                                           uint32_t group_id,
                                           uint32_t *total_consumed,
                                           uint32_t *total_released,
                                           uint32_t *entry_count)
{
    if (!ledger) return GMX_ERR_BAD_STATE;

    uint32_t tc = 0, tr = 0, ec = 0;
    for (uint32_t i = 0; i < ledger->count; i++) {
        const gmx_accounting_entry_t *e = &ledger->entries[i];
        if (!e->valid || e->group_id != group_id) continue;
        ec++;
        if (e->op == GMX_ACCT_CONSUME) tc += e->amount;
        if (e->op == GMX_ACCT_RELEASE) tr += e->amount;
    }

    if (total_consumed) *total_consumed = tc;
    if (total_released) *total_released = tr;
    if (entry_count) *entry_count = ec;
    return GMX_OK;
}

uint32_t gmx_accounting_net_consumption(const gmx_accounting_ledger_t *ledger)
{
    if (!ledger) return 0;
    if (ledger->total_consumed > ledger->total_released) {
        return ledger->total_consumed - ledger->total_released;
    }
    return 0;
}
