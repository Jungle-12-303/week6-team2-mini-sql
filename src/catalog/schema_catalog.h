#ifndef SCHEMA_CATALOG_H
#define SCHEMA_CATALOG_H

#include "mini_sql.h"

/* 한 테이블의 컬럼 이름/타입 목록을 메모리에 들고 있는 카탈로그 스키마다. */
typedef struct CatalogSchema {
    char **columns;
    char **types;
    size_t *max_lengths;
    bool *is_primary_keys;
    int primary_key_index;
    size_t column_count;
} CatalogSchema;

bool catalog_save_schema(const char *db_path, const char *table_name, const CatalogSchema *schema,
                         ErrorContext *err);
bool catalog_load_schema(const char *db_path, const char *table_name, CatalogSchema *schema,
                         ErrorContext *err);
bool catalog_copy_schema(const CatalogSchema *src, CatalogSchema *dst, ErrorContext *err);
void catalog_free_schema(CatalogSchema *schema);

#endif
