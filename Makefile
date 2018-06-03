PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man

INSTALL ?= install

CFLAGS := $(CFLAGS) -Wall -Wextra -std=c99 -pedantic -g -O0
CPPFLAGS := $(CPPFLAGS) -D_XOPEN_SOURCE=700

.PHONY: all clean install

all: reading

clean:
	rm -f reading

install: reading reading.1
	mkdir -p $(DESTDIR)$(BINDIR)
	$(INSTALL) reading $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -m 644 reading.1 $(DESTDIR)$(MANDIR)/man1

reading: reading.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o reading reading.c
