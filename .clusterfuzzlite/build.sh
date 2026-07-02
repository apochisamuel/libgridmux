#!/bin/bash

SRCS=$(find src/ -name '*.c' | sort)

for harness in channel_fuzzer session_fuzzer stream_fuzzer migrate_fuzzer; do
    $CC $CFLAGS -I include \
        fuzz/${harness}.c $SRCS \
        $LIB_FUZZING_ENGINE \
        -o $OUT/${harness}
done
