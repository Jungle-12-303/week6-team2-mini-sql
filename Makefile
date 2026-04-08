CC = gcc
CFLAGS = -Wall -Wextra -std=c11
TARGET = mini_sql_rebuild
SRCS = \
	08_parser_impl/parser.c \
	09_storage_impl/storage.c \
	10_executor_impl/executor.c \
	11_main/main.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

rebuild: clean all

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all rebuild clean
