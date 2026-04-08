#ifndef STORAGE_ENGINE_H
#define STORAGE_ENGINE_H

#include "catalog/schema_catalog.h"
#include "mini_sql.h"

typedef bool (*RowVisitorFn)(char **fields, size_t field_count, void *user_data,
                             ErrorContext *err);
typedef bool (*RowMatchFn)(char **fields, size_t field_count, void *user_data, bool *out_match,
                           ErrorContext *err);

typedef struct StorageEngineOps {
    bool (*load_schema)(StorageEngine *engine, const char *table_name, CatalogSchema *schema,
                        ErrorContext *err);
    bool (*create_table)(StorageEngine *engine, const char *table_name, const CatalogSchema *schema,
                         ErrorContext *err);
    bool (*drop_table)(StorageEngine *engine, const char *table_name, ErrorContext *err);
    bool (*append_row)(StorageEngine *engine, const char *table_name, char **fields, size_t field_count,
                       ErrorContext *err);
    bool (*scan_rows)(StorageEngine *engine, const char *table_name, RowVisitorFn visitor, void *user_data,
                      ErrorContext *err);
    bool (*delete_rows)(StorageEngine *engine, const char *table_name, RowMatchFn matcher, void *user_data,
                        size_t *out_deleted_count, ErrorContext *err);
    void (*destroy)(StorageEngine *engine);
} StorageEngineOps;

struct StorageEngine {
    const StorageEngineOps *ops;
    void *impl;
};

bool storage_engine_create(StorageEngine *engine, StorageEngineKind kind, const char *db_path,
                           ErrorContext *err);
bool storage_engine_load_schema(StorageEngine *engine, const char *table_name, CatalogSchema *schema,
                                ErrorContext *err);
bool storage_engine_create_table(StorageEngine *engine, const char *table_name, const CatalogSchema *schema,
                                 ErrorContext *err);
bool storage_engine_drop_table(StorageEngine *engine, const char *table_name, ErrorContext *err);
bool storage_engine_append_row(StorageEngine *engine, const char *table_name, char **fields, size_t field_count,
                               ErrorContext *err);
bool storage_engine_scan_rows(StorageEngine *engine, const char *table_name, RowVisitorFn visitor, void *user_data,
                              ErrorContext *err);
bool storage_engine_delete_rows(StorageEngine *engine, const char *table_name, RowMatchFn matcher, void *user_data,
                                size_t *out_deleted_count, ErrorContext *err);
void storage_engine_destroy(StorageEngine *engine);

#endif
