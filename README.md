# libgridmux

A multiplexed binary transport library for service mesh data planes.

## Overview

libgridmux provides a session-oriented wire protocol for multiplexing
channels, streams, and routes over a single transport connection.
It supports live channel migration between sessions, epoch-scoped
garbage collection, lease-based reference management, and per-tenant
quota enforcement.

## Architecture

The library is organized into 15 subsystems:

- **core** — arena allocator, byte utilities, type helpers
- **wire** — protocol encoding/decoding, frame validation
- **crc** — CRC-32C integrity checking
- **session** — lifecycle management, frame dispatch
- **channel** — flow control, credit windows, group membership
- **stream** — ordered delivery, checkpointing, replay
- **route** — topology management, cascade updates, segment data
- **registry** — object registration, epoch-scoped GC
- **lease** — reference counting, epoch-scoped expiration
- **compact** — route table compaction, deferred event processing
- **quota** — per-tenant resource limits, enforcement
- **journal** — write-ahead logging, checkpoint scanning
- **crypto** — session authentication, nonce anti-replay
- **migrate** — live channel/stream migration between sessions
- **slab** — pool-based memory management

## Wire Protocol

All data is exchanged as nested length-prefixed frames:

```
Session header (20 bytes):
  magic (4) + version (2) + session_id (4) + epoch (4) +
  frame_count (2) + header_crc (4)

Frame (34+ bytes):
  length (4) + frame_type (2) + sequence (4) + session_id (4) +
  channel_id (4) + route_id (4) + owner_token (4) + flags (2) +
  subrecord_count (2) + payload_crc (4) + subrecords...

Subrecord (16+ bytes):
  kind (2) + length (2) + local_epoch (4) + generation (4) +
  lease_token (4) + payload...
```

All integers are little-endian. CRC-32C (Castagnoli) is used for
integrity checking.

## Building

```bash
make            # build static library
make clean      # clean build artifacts
```

Requirements: C11-compatible compiler (clang or gcc), no external dependencies.

## Testing

```bash
# Unit tests
clang -std=c11 -I include tests/test_wire.c src/*.c -o test_wire && ./test_wire
clang -std=c11 -I include tests/test_session.c src/**/*.c -o test_session && ./test_session

# Robustness validation (requires libFuzzer)
clang -std=c11 -I include -fsanitize=fuzzer,address fuzz/session_fuzzer.c src/**/*.c -o fuzz_session
./fuzz_session corpus/ -max_total_time=300
```

## API Example

See `examples/basic_session.c` for a complete usage example demonstrating
session creation, route advertisement, channel binding, and data forwarding.

## License

MIT — see LICENSE file.
