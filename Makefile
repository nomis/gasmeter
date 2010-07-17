CFLAGS=-Wall -Wextra -Wshadow -O2 -D_POSIX_SOURCE -DVERBOSE
LDFLAGS=-lrt
.PHONY: all clean
all: pulsemon
clean:
	rm -f pulsemon
pulsemon: pulsemon.c pulsemon.h pulseq.h Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
