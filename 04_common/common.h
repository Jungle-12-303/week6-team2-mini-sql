#ifndef COMMON_H
#define COMMON_H

#define MAX_TABLE_NAME_LENGTH 128
#define MAX_VALUES 16
#define MAX_ROWS 256
#define MAX_CELL_LENGTH 128

typedef enum {
    COMMAND_UNKNOWN = 0,
    COMMAND_INSERT,
    COMMAND_SELECT
} CommandType;

typedef struct {
    CommandType type;
    char table_name[MAX_TABLE_NAME_LENGTH];
    char values[MAX_VALUES][128];
    int value_count;
} Command;

#endif
