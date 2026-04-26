CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lpthread -lrt

all: mcached client

mcached: mcached.c mcached.h
	$(CC) $(CFLAGS) -o mcached mcached.c $(LDFLAGS)

client: client.c mcached.h
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f mcached client *.o

.PHONY: all clean