CC = cc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -g -Iinclude
DOCKER_COMPOSE = docker compose -f .devcontainer/docker-compose.yml
DOCKER_BOOTSTRAP = bash .devcontainer/bootstrap.sh
OS_NAME = $(shell uname -s | tr '[:upper:]' '[:lower:]')
ARCH_NAME = $(shell uname -m)
BUILD_DIR ?= .build/$(OS_NAME)-$(ARCH_NAME)
MINI_SQL_BIN = $(BUILD_DIR)/mini_sql.bin
TEST_SUITE_BIN = $(BUILD_DIR)/test_suite.bin

COMMON_SRCS = \
	src/utils.c \
	src/lexer.c \
	src/parser.c \
	src/storage.c \
	src/executor.c

APP_SRCS = src/main.c $(COMMON_SRCS)
TEST_SRCS = tests/test_suite.c $(COMMON_SRCS)

.PHONY: all clean test demo cli local-cli docker-build docker-test docker-cli docker-demo docker-shell docker-down

all: mini_sql

mini_sql: $(MINI_SQL_BIN)

test_suite: $(TEST_SUITE_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(MINI_SQL_BIN): $(APP_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(APP_SRCS)

$(TEST_SUITE_BIN): $(TEST_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS)

test: $(MINI_SQL_BIN) $(TEST_SUITE_BIN)
	$(TEST_SUITE_BIN)
	./tests/test_cli.sh

demo: $(MINI_SQL_BIN)
	./scripts/demo.sh

cli: docker-cli

local-cli: $(MINI_SQL_BIN)
	./mini_sql --db ./db

docker-build:
	$(DOCKER_COMPOSE) up -d dev
	$(DOCKER_COMPOSE) exec -T dev $(DOCKER_BOOTSTRAP)

docker-test: docker-build
	$(DOCKER_COMPOSE) exec -T dev bash -lc "make test"

docker-cli: docker-build
	$(DOCKER_COMPOSE) exec dev bash -lc "./mini_sql --db ./db"

docker-demo: docker-build
	$(DOCKER_COMPOSE) exec -T dev bash -lc "make demo"

docker-shell: docker-build
	$(DOCKER_COMPOSE) exec dev bash

docker-down:
	$(DOCKER_COMPOSE) down

clean:
	rm -rf .build .tmp
