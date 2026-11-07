# Host-side unit tests for the HAL/IDF-free modules: `make -C tests`
CC     ?= cc
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -I../main

TESTS := test_protocol test_command_parser

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; echo; done

test_protocol: test_protocol.c ../main/protocol.c ../main/protocol.h
	$(CC) $(CFLAGS) test_protocol.c ../main/protocol.c -o $@

test_command_parser: test_command_parser.c ../main/command_parser.c ../main/protocol.h
	$(CC) $(CFLAGS) test_command_parser.c ../main/command_parser.c -o $@

clean:
	rm -f $(TESTS)

.PHONY: test clean
