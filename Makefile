CFLAGS=-Wall -Wextra -Wshadow -O2 -ggdb -D_POSIX_SOURCE -D_ISOC99_SOURCE -DVERBOSE -DFORK
LDFLAGS=-Wl,--as-needed
MQ_LIBS=-lrt
DB_LIBS=-lpq
.PHONY: all clean
all: pulsemon pulsedb pulsefake
clean:
	rm -f pulsemon pulsedb pulsefake

pulsemon: pulsemon.c pulsemon.h pulseq.h Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(MQ_LIBS)

pulsedb: pulsedb.c pulsedb.h pulseq.h Makefile pulsedb_postgres.c pulsedb_postgres.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(MQ_LIBS) pulsedb_postgres.c $(DB_LIBS)

pulsefake: pulsefake.c pulsefake.h pulseq.h Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(MQ_LIBS)
