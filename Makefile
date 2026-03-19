CC=cc
CFLAGS=-Wall -Werror

gol: gol.c
	$(CC) $(CFLAGS) -o gol gol.c -lSDL2

