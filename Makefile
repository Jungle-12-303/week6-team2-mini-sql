CC = cc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -g -Iinclude -Isrc
DOCKER_COMPOSE = docker compose -f .devcontainer/docker-compose.yml
DOCKER_BOOTSTRAP = bash .devcontainer/bootstrap.sh
OS_NAME = $(shell uname -s | tr '[:upper:]' '[:lower:]')
ARCH_NAME = $(shell uname -m)
ARTIFACTS_DIR ?= .artifacts
BUILD_ROOT ?= $(ARTIFACTS_DIR)/build
RUNTIME_DIR ?= $(ARTIFACTS_DIR)/runtime
ANALYZE_DIR ?= $(ARTIFACTS_DIR)/analyze
BUILD_DIR ?= $(BUILD_ROOT)/$(OS_NAME)-$(ARCH_NAME)
MINI_SQL_BIN = $(BUILD_DIR)/mini_sql.bin
TEST_SUITE_BIN = $(BUILD_DIR)/test_suite.bin

COMMON_SRCS = $(shell find src -type f -name '*.c' ! -path 'src/main.c' | sort)
APP_SRCS = src/main.c $(COMMON_SRCS)
TEST_SRCS = tests/test_suite.c $(COMMON_SRCS)

.PHONY: all clean test demo cli local-cli docker-build docker-test docker-cli docker-demo docker-shell docker-down analyze

all: mini_sql

mini_sql: $(MINI_SQL_BIN)

test_suite: $(TEST_SUITE_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(RUNTIME_DIR):
	mkdir -p $(RUNTIME_DIR)

$(ANALYZE_DIR):
	mkdir -p $(ANALYZE_DIR)

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

analyze: | $(ANALYZE_DIR)
	rm -f $(ANALYZE_DIR)/*.plist
	for src in $$(find src -type f -name '*.c' | sort); do \
		base=$$(basename $$src .c); \
		clang --analyze -Xanalyzer -analyzer-checker=deadcode.DeadStores \
			-Xclang -analyzer-output=plist \
			-o $(ANALYZE_DIR)/$$base.plist $$src -Iinclude -Isrc; \
	done

clean:
	rm -rf $(ARTIFACTS_DIR) .build .tmp
