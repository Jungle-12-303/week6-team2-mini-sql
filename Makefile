CC = cc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -g -Iinclude

COMMON_SRCS = \
	src/utils.c \
	src/lexer.c \
	src/parser.c \
	src/storage.c \
	src/executor.c

APP_SRCS = src/main.c $(COMMON_SRCS)
TEST_SRCS = tests/test_suite.c $(COMMON_SRCS)

.PHONY: all clean test demo cli

all: mini_sql

mini_sql: $(APP_SRCS)
	$(CC) $(CFLAGS) -o $@ $(APP_SRCS)

test_suite: $(TEST_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS)

test: mini_sql test_suite
	./test_suite
	./tests/test_cli.sh

demo: mini_sql
	./scripts/demo.sh

cli: mini_sql
	./mini_sql --db ./db

clean:
	rm -f mini_sql test_suite
	rm -rf .tmp
