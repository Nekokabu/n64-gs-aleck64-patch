CC     ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -pedantic

PROG = gs_aleck64_patch
SRCS = gs_aleck64_patch.c lzari.c

.PHONY: all clean

all: $(PROG)

$(PROG): $(SRCS) lzari.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(PROG) $(PROG).exe
