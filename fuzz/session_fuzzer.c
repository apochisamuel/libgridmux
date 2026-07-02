/*
 * libgridmux - Session protocol robustness harness
 *
 * Exercises session lifecycle, channel/stream management, routing,
 * epoch advancement, and compaction through the public wire-format
 * ingest API.
 */

#include "gmx/types.h"
#include "gmx/session.h"
#include "gmx/wire.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < GMX_SESSION_HEADER_WIRE_SIZE + GMX_FRAME_HEADER_WIRE_SIZE) {
        return 0;
    }

    /* Extract session_id from wire data for session creation */
    uint32_t session_id = gmx_wire_read_u32(data + 6);  /* offset of session_id */
    if (session_id == 0) {
        session_id = 1;
    }

    gmx_session_t *sess = gmx_session_create(session_id);
    if (!sess) {
        return 0;
    }

    /* Ingest the fuzz input through the public API */
    gmx_session_ingest(sess, data, size);

    /* Clean up: dependents before dependencies */
    gmx_session_destroy(sess);

    return 0;
}
