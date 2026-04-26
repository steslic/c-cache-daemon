CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lpthread -lrt

all: mcached

mcached: mcached.c mcached.h
	$(CC) $(CFLAGS) -o mcached mcached.c $(LDFLAGS)

clean:
	rm -f mcached client *.o

.PHONY: all clean