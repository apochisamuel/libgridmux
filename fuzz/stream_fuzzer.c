/*
 * libgridmux - Stream lifecycle robustness harness
 *
 * Exercises stream creation, ordered data writes, checkpointing,
 * lease management, journal operations, and data forwarding through
 * the session ingest API. Focus on stream-centric lifecycle
 * interactions with journal and lease subsystems.
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

    uint32_t session_id = gmx_wire_read_u32(data + 6);
    if (session_id == 0) session_id = 1;

    gmx_session_t *sess = gmx_session_create(session_id);
    if (!sess) return 0;

    gmx_session_ingest(sess, data, size);
    gmx_session_destroy(sess);

    return 0;
}
