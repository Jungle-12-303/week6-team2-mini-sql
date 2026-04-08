#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdio.h>

#define MAX_SQL_LENGTH 1024
#define MAX_TABLE_NAME 64
#define MAX_VALUES 16
#define MAX_VALUE_LENGTH 128
#define MAX_SELECT_COLUMNS 16
#define MAX_COLUMN_NAME 64
#define MAX_LINE_LENGTH 1024
#define MAX_PATH_LENGTH 256
#define MAX_ERROR_LENGTH 256
#define DEFAULT_DATA_DIR "data"

typedef enum StatementType {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct Statement {
    StatementType type;
    char table_name[MAX_TABLE_NAME];
    int value_count;
    char values[MAX_VALUES][MAX_VALUE_LENGTH];
    int select_all;
    int select_column_count;
    char select_columns[MAX_SELECT_COLUMNS][MAX_COLUMN_NAME];
} Statement;

#endif
