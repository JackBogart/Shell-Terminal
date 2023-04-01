CC     = gcc
CFLAGS = -g -std=c99 -Wall -Wvla -Werror -fsanitize=address,undefined

all: myShell

myShell: myShell.c
		$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f *.o myShell
