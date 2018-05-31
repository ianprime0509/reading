PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man

CFLAGS := $(CFLAGS) -Wall -Wextra -std=c99 -pedantic -g -O0
CPPFLAGS := $(CPPFLAGS) -D_XOPEN_SOURCE=700

.PHONY: all clean install

all: reading

clean:
	\rm -f reading

reading: reading.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o reading reading.c
