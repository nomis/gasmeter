CFLAGS=-Wall -Wextra -Wshadow -O2 -ggdb -D_POSIX_SOURCE -DVERBOSE
LDFLAGS=-lrt -lpq
.PHONY: all clean
all: pulsemon pulsedb pulsefake
clean:
	rm -f pulsemon pulsedb pulsefake

pulsemon: pulsemon.c pulsemon.h pulseq.h Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

pulsedb: pulsedb.c pulsedb.h pulseq.h Makefile pulsedb_postgres.c pulsedb_postgres.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< pulsedb_postgres.c

pulsefake: pulsefake.c pulsefake.h pulseq.h Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
