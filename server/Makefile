CC ?= gcc
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -std=c11 -O2
WARNINGS ?= -Wall -Wextra -Wpedantic -Werror
THREADS ?= -pthread
SRC := src/server.c src/game.c src/net.c src/state.c src/commands.c
HEADERS := $(wildcard include/*.h)
BIN := connect_four_server

all: $(BIN)

$(BIN): $(SRC) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(THREADS) $(SRC) -o $(BIN)

debug: CFLAGS = -std=c11 -O0 -g3 -fsanitize=address,undefined
debug: WARNINGS = -Wall -Wextra -Wpedantic
debug: $(BIN)

clean:
	$(RM) $(BIN)

.PHONY: all debug clean
