CC = gcc
CFLAGS = -std=c99 -g

tgrep: tgrep.c tgrep.h
	$(CC) tgrep.c $(CFLAGS) -o $@
