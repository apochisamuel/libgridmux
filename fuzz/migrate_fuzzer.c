/*
 * libgridmux - Migration lifecycle robustness harness
 *
 * Exercises live channel/stream migration between two sessions.
 * Creates a source and destination session, processes migration
 * preparation, transfer, and commit operations through the
 * wire protocol ingest API.
 *
 * This harness uses two sessions to model the source→destination
 * migration topology. The fuzz input is split: first half drives
 * the source session, second half drives the destination.
 */

#include "gmx/types.h"
#include "gmx/session.h"
#include "gmx/wire.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < (GMX_SESSION_HEADER_WIRE_SIZE + GMX_FRAME_HEADER_WIRE_SIZE) * 2) {
        return 0;
    }

    /* Split input: first half for source, second half for destination */
    size_t split = size / 2;

    const uint8_t *src_data = data;
    size_t src_len = split;
    const uint8_t *dst_data = data + split;
    size_t dst_len = size - split;

    /* Extract session IDs */
    uint32_t src_session_id = 0;
    uint32_t dst_session_id = 0;
    if (src_len >= 10) {
        src_session_id = gmx_wire_read_u32(src_data + 6);
    }
    if (dst_len >= 10) {
        dst_session_id = gmx_wire_read_u32(dst_data + 6);
    }
    if (src_session_id == 0) src_session_id = 1;
    if (dst_session_id == 0) dst_session_id = 2;
    if (src_session_id == dst_session_id) dst_session_id = src_session_id + 1;

    /* Create source and destination sessions */
    gmx_session_t *src_sess = gmx_session_create(src_session_id);
    if (!src_sess) return 0;

    gmx_session_t *dst_sess = gmx_session_create(dst_session_id);
    if (!dst_sess) {
        gmx_session_destroy(src_sess);
        return 0;
    }

    /* Process source session frames */
    gmx_session_ingest(src_sess, src_data, src_len);

    /* Process destination session frames */
    gmx_session_ingest(dst_sess, dst_data, dst_len);

    /* Cleanup: destroy in correct order */
    gmx_session_destroy(dst_sess);
    gmx_session_destroy(src_sess);

    return 0;
}
