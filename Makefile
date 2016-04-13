CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99 -g3
BIN=proj02

ALL: $(BIN)

$(BIN): proj02.o
	$(CC) $(CFLAGS) proj02.o -o $@ -lpthread

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BIN) *.o 2>/dev/null
