CC=gcc
CFLAGS=-W -Wall -std=c99 -Os
LDFLAGS=-lm

all: net-integrate

SOURCES=main.c net.c integrate.c list.c ui.c cpuconf.c
net-integrate: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $@ $(LDFLAGS)

clean:
	rm -rf net-integrate

integrate: list.h ui.h general.h cpuconf.h integrate.h net.h
