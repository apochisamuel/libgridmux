CC       ?= clang
CFLAGS   ?= -std=c11 -Wall -Wextra -Wpedantic -Wno-unused-parameter
CFLAGS   += -I include
LDFLAGS  ?=

SRCS = src/core/error.c \
       src/core/arena.c \
       src/core/bitops.c \
       src/crc/crc32.c \
       src/wire/wire_decode.c \
       src/wire/wire_encode.c \
       src/wire/wire_frame.c \
       src/slab/slab.c \
       src/registry/registry.c \
       src/route/route.c \
       src/route/route_cascade.c \
       src/channel/channel.c \
       src/channel/channel_group.c \
       src/stream/stream.c \
       src/stream/stream_checkpoint.c \
       src/compact/compact.c \
       src/compact/compact_deferred.c \
       src/lease/lease.c \
       src/quota/quota.c \
       src/journal/journal.c \
       src/crypto/crypto.c \
       src/migrate/migrate.c \
       src/session/session.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: libgridmux.a

libgridmux.a: $(OBJS)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) libgridmux.a
