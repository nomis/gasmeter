CFLAGS=-Wall -Wextra -Wshadow -O2
LDFLAGS=-lrt
.PHONY: all clean
all: pulsemon
clean:
	rm -f pulsemon
pulsemon: pulsemon.c pulsemon.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
