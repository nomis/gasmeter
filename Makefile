CFLAGS=-Wall -Wextra -O2
.PHONY: all clean
all: pulsemon
clean:
	rm -f pulsemon
pulsemon: pulsemon.c pulsemon.h
	$(CC) $(CFLAGS) -o $@ $<
