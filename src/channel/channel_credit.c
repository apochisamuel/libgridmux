/*
 * libgridmux - Channel credit window accounting
 * Copyright (c) 2024-2025 GridMux Contributors
 * SPDX-License-Identifier: MIT
 *
 * Implements per-channel credit-based flow control with window
 * management, debt tracking, and fairness enforcement.
 */
#include "gmx/channel.h"
#include "gmx/types.h"
#include "gmx/error.h"
#include <string.h>

#define GMX_CREDIT_HISTORY_SIZE  16

typedef struct gmx_credit_entry {
    uint32_t amount;
    uint32_t epoch;
    uint32_t sequence;
    bool     is_grant;
} gmx_credit_entry_t;

typedef struct gmx_credit_tracker {
    uint32_t channel_id;
    uint32_t total_granted;
    uint32_t total_consumed;
    uint32_t peak_outstanding;
    uint32_t current_outstanding;
    uint32_t pending_debt;
    gmx_credit_entry_t history[GMX_CREDIT_HISTORY_SIZE];
    uint32_t hist_count;
    uint32_t hist_pos;
} gmx_credit_tracker_t;

gmx_status_t gmx_credit_tracker_init(gmx_credit_tracker_t *ct,
                                        uint32_t channel_id)
{
    if (!ct) return GMX_ERR_BAD_STATE;
    memset(ct, 0, sizeof(*ct));
    ct->channel_id = channel_id;
    return GMX_OK;
}

gmx_status_t gmx_credit_record_grant(gmx_credit_tracker_t *ct,
                                        uint32_t amount,
                                        uint32_t epoch, uint32_t sequence)
{
    if (!ct || amount == 0) return GMX_ERR_BAD_STATE;
    ct->total_granted += amount;
    ct->current_outstanding += amount;
    if (ct->current_outstanding > ct->peak_outstanding) {
        ct->peak_outstanding = ct->current_outstanding;
    }
    uint32_t idx = ct->hist_pos % GMX_CREDIT_HISTORY_SIZE;
    ct->history[idx].amount = amount;
    ct->history[idx].epoch = epoch;
    ct->history[idx].sequence = sequence;
    ct->history[idx].is_grant = true;
    ct->hist_pos++;
    if (ct->hist_count < GMX_CREDIT_HISTORY_SIZE) ct->hist_count++;
    return GMX_OK;
}

gmx_status_t gmx_credit_record_consume(gmx_credit_tracker_t *ct,
                                          uint32_t amount,
                                          uint32_t epoch, uint32_t sequence)
{
    if (!ct || amount == 0) return GMX_ERR_BAD_STATE;
    ct->total_consumed += amount;
    if (amount <= ct->current_outstanding) {
        ct->current_outstanding -= amount;
    } else {
        ct->pending_debt += amount - ct->current_outstanding;
        ct->current_outstanding = 0;
    }
    uint32_t idx = ct->hist_pos % GMX_CREDIT_HISTORY_SIZE;
    ct->history[idx].amount = amount;
    ct->history[idx].epoch = epoch;
    ct->history[idx].sequence = sequence;
    ct->history[idx].is_grant = false;
    ct->hist_pos++;
    if (ct->hist_count < GMX_CREDIT_HISTORY_SIZE) ct->hist_count++;
    return GMX_OK;
}

uint32_t gmx_credit_available(const gmx_credit_tracker_t *ct)
{
    if (!ct) return 0;
    return ct->current_outstanding;
}

uint32_t gmx_credit_debt(const gmx_credit_tracker_t *ct)
{
    return ct ? ct->pending_debt : 0;
}

float gmx_credit_utilization(const gmx_credit_tracker_t *ct)
{
    if (!ct || ct->total_granted == 0) return 0.0f;
    return (float)ct->total_consumed / (float)ct->total_granted;
}

gmx_status_t gmx_credit_snapshot(const gmx_credit_tracker_t *ct,
                                    uint32_t *granted, uint32_t *consumed,
                                    uint32_t *outstanding, uint32_t *debt)
{
    if (!ct) return GMX_ERR_BAD_STATE;
    if (granted) *granted = ct->total_granted;
    if (consumed) *consumed = ct->total_consumed;
    if (outstanding) *outstanding = ct->current_outstanding;
    if (debt) *debt = ct->pending_debt;
    return GMX_OK;
}

gmx_status_t gmx_credit_apply_debt(gmx_credit_tracker_t *ct,
                                      uint32_t settlement)
{
    if (!ct) return GMX_ERR_BAD_STATE;
    if (settlement > ct->pending_debt) {
        ct->pending_debt = 0;
    } else {
        ct->pending_debt -= settlement;
    }
    return GMX_OK;
}
