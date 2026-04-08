#include "catalog/schema_catalog.h"
#include "storage/csv_codec.h"
#include "storage/storage_path.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * schema_catalog.c 는 "테이블 스키마 메타데이터" 전담 모듈이다.
 *
 * 경로 계산과 디렉터리 생성은 storage_path.c 로,
 * CSV 직렬화/역직렬화는 csv_codec.c 로 분리되어 있고,
 * 여기서는 .schema 포맷의 저장/복원과 CatalogSchema 메모리 관리만 담당한다.
 */

#define SCHEMA_VERSION_HEADER "#mini_sql_schema_v2"
#define DEFAULT_COLUMN_TYPE "TEXT"

/* src 의 컬럼/타입을 dst 로 깊은 복사한다. 실패 시 dst 는 비어 있는 상태로 남는다. */
bool catalog_copy_schema(const CatalogSchema *src, CatalogSchema *dst, ErrorContext *err) {
    size_t i;

    dst->columns = NULL;
    dst->types   = NULL;
    dst->column_count = 0U;

    if (src->column_count == 0U) {
        return true;
    }

    dst->columns = calloc(src->column_count, sizeof(*dst->columns));
    dst->types   = calloc(src->column_count, sizeof(*dst->types));
    if (dst->columns == NULL || dst->types == NULL) {
        free(dst->columns);
        free(dst->types);
        dst->columns = NULL;
        dst->types   = NULL;
        set_error(err, "스키마를 복사하는 중 메모리가 부족합니다");
        return false;
    }

    for (i = 0; i < src->column_count; ++i) {
        dst->columns[i] = msql_strdup(src->columns[i]);
        dst->types[i]   = msql_strdup(src->types[i]);
        if (dst->columns[i] == NULL || dst->types[i] == NULL) {
            dst->column_count = i;  /* catalog_free_schema 가 i 개까지만 해제하도록 */
            catalog_free_schema(dst);
            set_error(err, "스키마를 복사하는 중 메모리가 부족합니다");
            return false;
        }
        dst->column_count += 1U;
    }

    return true;
}

/* CatalogSchema 내부 컬럼/타입 메모리를 전부 정리한다. */
void catalog_free_schema(CatalogSchema *schema) {
    size_t i;

    if (schema == NULL) {
        return;
    }

    for (i = 0; i < schema->column_count; ++i) {
        free(schema->columns[i]);
        free(schema->types[i]);
    }
    free(schema->columns);
    free(schema->types);
    schema->columns = NULL;
    schema->types = NULL;
    schema->column_count = 0U;
}

/* schema 에 컬럼 이름/타입 한 쌍을 추가한다. */
static bool append_table_schema_entry(CatalogSchema *schema, const char *name, const char *type,
                                      ErrorContext *err) {
    char **new_columns;
    char **new_types;
    char *name_copy;
    char *type_copy;

    new_columns = realloc(schema->columns, (schema->column_count + 1U) * sizeof(*new_columns));
    if (new_columns == NULL) {
        set_error(err, "스키마를 만드는 중 메모리가 부족합니다");
        return false;
    }
    schema->columns = new_columns;

    new_types = realloc(schema->types, (schema->column_count + 1U) * sizeof(*new_types));
    if (new_types == NULL) {
        set_error(err, "스키마를 만드는 중 메모리가 부족합니다");
        return false;
    }
    schema->types = new_types;

    name_copy = msql_strdup(name);
    type_copy = msql_strdup(type == NULL ? DEFAULT_COLUMN_TYPE : type);
    if (name_copy == NULL || type_copy == NULL) {
        free(name_copy);
        free(type_copy);
        set_error(err, "스키마를 만드는 중 메모리가 부족합니다");
        return false;
    }

    schema->columns[schema->column_count] = name_copy;
    schema->types[schema->column_count] = type_copy;
    schema->column_count += 1U;
    return true;
}

/*
 * 현재 프로젝트의 .schema 포맷으로 스키마를 저장한다.
 * 첫 줄은 버전 헤더, 이후 줄은 "컬럼명,타입" CSV 한 줄씩이다.
 */
bool catalog_save_schema(const char *db_path, const char *table_name, const CatalogSchema *schema,
                       ErrorContext *err) {
    char *path = build_table_path(db_path, table_name, ".schema");
    FILE *file = NULL;
    size_t i;

    if (path == NULL) {
        set_error(err, "스키마를 저장하는 중 메모리가 부족합니다");
        return false;
    }

    if (!ensure_parent_directories(path, err)) {
        free(path);
        return false;
    }

    file = fopen(path, "w");
    if (file == NULL) {
        set_error(err, "스키마 파일을 열지 못했습니다 %s: %s", path, strerror(errno));
        free(path);
        return false;
    }

    if (fprintf(file, "%s\n", SCHEMA_VERSION_HEADER) < 0) {
        set_error(err, "스키마 헤더를 쓰지 못했습니다");
        fclose(file);
        free(path);
        return false;
    }

    for (i = 0; i < schema->column_count; ++i) {
        char *row_fields[2];

        row_fields[0] = schema->columns[i];
        row_fields[1] = schema->types[i];
        if (!write_csv_row(file, row_fields, 2U, err)) {
            fclose(file);
            free(path);
            return false;
        }
    }

    fclose(file);
    free(path);
    return true;
}

/*
 * .schema 파일을 읽어 CatalogSchema 로 복원한다.
 * 새 버전 포맷과 예전 one-line 컬럼 목록 포맷을 모두 지원해서 호환성을 유지한다.
 */
bool catalog_load_schema(const char *db_path, const char *table_name, CatalogSchema *schema,
                       ErrorContext *err) {
    char *path = build_table_path(db_path, table_name, ".schema");
    char *contents = NULL;
    char *line;
    bool new_format = false;

    schema->columns = NULL;
    schema->types = NULL;
    schema->column_count = 0U;

    if (path == NULL) {
        set_error(err, "스키마를 읽는 중 메모리가 부족합니다");
        return false;
    }

    if (!read_file_all(path, &contents, err)) {
        free(path);
        return false;
    }
    free(path);

    line = strtok(contents, "\n");
    while (line != NULL && trim_in_place(line)[0] == '\0') {
        line = strtok(NULL, "\n");
    }

    if (line == NULL) {
        free(contents);
        set_error(err, "테이블 %s의 스키마 파일이 비어 있습니다", table_name);
        return false;
    }

    /* 새 포맷이면 헤더 다음 줄부터 "컬럼명,타입"을 읽는다. */
    if (strings_equal_ci(trim_in_place(line), SCHEMA_VERSION_HEADER)) {
        new_format = true;
        line = strtok(NULL, "\n");
        while (line != NULL) {
            char *trimmed = trim_in_place(line);
            char **fields = NULL;
            size_t field_count = 0U;
            bool ok;

            if (trimmed[0] == '\0') {
                line = strtok(NULL, "\n");
                continue;
            }

            ok = parse_csv_line(trimmed, &fields, &field_count, err);
            if (!ok) {
                free(contents);
                catalog_free_schema(schema);
                return false;
            }

            ok = append_table_schema_entry(schema, trim_in_place(fields[0]),
                                           field_count > 1U ? trim_in_place(fields[1]) : DEFAULT_COLUMN_TYPE,
                                           err);
            free_string_array(fields, field_count);
            if (!ok) {
                free(contents);
                catalog_free_schema(schema);
                return false;
            }

            line = strtok(NULL, "\n");
        }
    } else {
        /* 예전 포맷이면 첫 줄의 CSV 컬럼 목록을 모두 TEXT 타입으로 간주한다. */
        char **raw_fields = NULL;
        size_t raw_count = 0U;
        size_t i;

        if (!parse_csv_line(line, &raw_fields, &raw_count, err)) {
            free(contents);
            return false;
        }

        for (i = 0; i < raw_count; ++i) {
            if (!append_table_schema_entry(schema, trim_in_place(raw_fields[i]), DEFAULT_COLUMN_TYPE,
                                           err)) {
                free(contents);
                free_string_array(raw_fields, raw_count);
                catalog_free_schema(schema);
                return false;
            }
        }
        free_string_array(raw_fields, raw_count);
    }

    free(contents);

    if (schema->column_count == 0U) {
        set_error(err, new_format ? "스키마 정의에 컬럼이 없습니다" : "스키마 파일이 비어 있습니다");
        catalog_free_schema(schema);
        return false;
    }

    return true;
}
