#ifndef STORAGE_ENGINE_H
#define STORAGE_ENGINE_H

#include "mini_sql.h"
#include "table_file.h"

typedef bool (*RowVisitorFn)(char **fields, size_t field_count, void *user_data,
                             char *error_buf, size_t error_size);

typedef struct StorageEngineOps {
    bool (*load_schema)(StorageEngine *engine, const char *table_name, TableSchema *schema,
                        char *error_buf, size_t error_size);
    bool (*save_schema)(StorageEngine *engine, const char *table_name, const TableSchema *schema,
                        char *error_buf, size_t error_size);
    bool (*append_row)(StorageEngine *engine, const char *table_name, char **fields, size_t field_count,
                       char *error_buf, size_t error_size);
    bool (*scan_rows)(StorageEngine *engine, const char *table_name, RowVisitorFn visitor, void *user_data,
                      char *error_buf, size_t error_size);
    void (*destroy)(StorageEngine *engine);
} StorageEngineOps;

struct StorageEngine {
    const StorageEngineOps *ops;
    void *impl;
};

bool storage_engine_create_file(StorageEngine *engine, const char *db_path, char *error_buf, size_t error_size);
bool storage_engine_load_schema(StorageEngine *engine, const char *table_name, TableSchema *schema,
                                char *error_buf, size_t error_size);
bool storage_engine_save_schema(StorageEngine *engine, const char *table_name, const TableSchema *schema,
                                char *error_buf, size_t error_size);
bool storage_engine_append_row(StorageEngine *engine, const char *table_name, char **fields, size_t field_count,
                               char *error_buf, size_t error_size);
bool storage_engine_scan_rows(StorageEngine *engine, const char *table_name, RowVisitorFn visitor, void *user_data,
                              char *error_buf, size_t error_size);
void storage_engine_destroy(StorageEngine *engine);

#endif
