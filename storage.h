#ifndef STORAGE_H
#define STORAGE_H

#include "query.h"

#define MAX_COLUMNS 3
#define MAX_ROWS 100

typedef struct {
    int column_count;
    char names[MAX_COLUMNS][32];
    char types[MAX_COLUMNS][16];
} TableSchema;

typedef struct {
    int row_count;
    char values[MAX_ROWS][MAX_COLUMNS][32];
} TableData;

int load_schema(const char *table_name, TableSchema *schema);
int load_rows(const char *table_name, TableData *data);
int append_user(const Query *query);
void list_tables(void);
int print_schema(const char *table_name);

#endif
