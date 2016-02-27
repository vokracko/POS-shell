CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99 -g3
BIN=shell

ALL: $(BIN)

$(BIN): shell.o
	$(CC) $(CFLAGS) $< -o $@ -lpthread

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BIN) *.o 2>/dev/null
