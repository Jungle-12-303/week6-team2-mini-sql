#ifndef TABLE_FILE_H
#define TABLE_FILE_H

#include "mini_sql.h"

typedef struct TableSchema {
    char **columns;
    size_t column_count;
} TableSchema;

char *trim_in_place(char *text);
char *build_table_path(const char *db_path, const char *table_name, const char *extension);
bool ensure_parent_directories(const char *path, char *error_buf, size_t error_size);
bool parse_csv_line(const char *line, char ***out_fields, size_t *out_count, char *error_buf, size_t error_size);
bool write_csv_row(FILE *file, char **fields, size_t field_count, char *error_buf, size_t error_size);
bool load_table_schema(const char *db_path, const char *table_name, TableSchema *schema, char *error_buf, size_t error_size);
void free_table_schema(TableSchema *schema);

#endif
