#ifndef SCHEMA_CATALOG_H
#define SCHEMA_CATALOG_H

#include "mini_sql.h"

/* 한 테이블의 컬럼 이름/타입 목록을 메모리에 들고 있는 카탈로그 스키마다. */
typedef struct CatalogSchema {
    char **columns;
    char **types;
    size_t *max_lengths;
    bool *is_primary_keys;
    size_t *storage_slots;
    size_t *logical_indexes_by_storage_slot;
    int primary_key_index;
    size_t column_count;
} CatalogSchema;

bool catalog_save_schema(const char *db_path, const char *table_name, const CatalogSchema *schema,
                         ErrorContext *err);
bool catalog_load_schema(const char *db_path, const char *table_name, CatalogSchema *schema,
                         ErrorContext *err);
bool catalog_copy_schema(const CatalogSchema *src, CatalogSchema *dst, ErrorContext *err);
bool catalog_assign_storage_layout(CatalogSchema *schema, ErrorContext *err);
size_t catalog_schema_storage_slot(const CatalogSchema *schema, size_t logical_index);
size_t catalog_schema_logical_index(const CatalogSchema *schema, size_t storage_slot);
bool catalog_build_storage_row(const CatalogSchema *schema, char **logical_fields, size_t field_count,
                               char ***out_storage_fields, ErrorContext *err);
bool catalog_build_logical_row(const CatalogSchema *schema, char **storage_fields, size_t field_count,
                               char ***out_logical_fields, ErrorContext *err);
void catalog_free_schema(CatalogSchema *schema);

#endif
