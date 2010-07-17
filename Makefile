CFLAGS=-Wall -Wextra -Wshadow -O2 -D_POSIX_SOURCE -DVERBOSE
LDFLAGS=-lrt
.PHONY: all clean
all: pulsemon pulsedb
clean:
	rm -f pulsemon pulseb

pulsemon: pulsemon.c pulsemon.h pulseq.h Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

pulsedb: pulsedb.c pulsedb.h pulseq.h Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
