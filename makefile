CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread

mts: mts.c
	$(CC) $(CFLAGS) -o mts mts.c

clean:
	rm -f mts

