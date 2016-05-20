CFLAGS=-Wall -Wextra -Wshadow -O2 -ggdb -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600 -D_ISOC99_SOURCE -DVERBOSE -DFORK
LDFLAGS=-Wl,--as-needed
MQ_LIBS=-lrt
DB_LIBS=-lpq
INSTALL=install

.PHONY: all clean install

all: pulsemon pulsedb heatingdb pulsefake
clean:
	rm -f pulsemon pulsedb heatingdb pulsefake

prefix=/usr
exec_prefix=$(prefix)
libdir=$(exec_prefix)/lib

install: all
	$(INSTALL) -m 755 -D pulsedb $(DESTDIR)$(libdir)/arduino-mux/pulsedb
	$(INSTALL) -m 755 -D heatingdb $(DESTDIR)$(libdir)/arduino-mux/heatingdb

pulsemon: pulsemon.c pulsemon.h pulseq.h Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(MQ_LIBS)

pulsedb: pulsedb.c pulsedb.h pulseq.h Makefile pulsedb_postgres.c pulsedb_postgres.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(MQ_LIBS) pulsedb_postgres.c $(DB_LIBS)

heatingdb: pulsedb.c pulsedb.h pulseq.h Makefile pulsedb_postgres.c pulsedb_postgres.h
	$(CC) $(CFLAGS) $(LDFLAGS) '-DTABLE="heating"' '-DNO_RESET' -o $@ $< $(MQ_LIBS) pulsedb_postgres.c $(DB_LIBS)

pulsefake: pulsefake.c pulsefake.h pulseq.h Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(MQ_LIBS)
