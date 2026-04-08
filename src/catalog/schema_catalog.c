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

#define SCHEMA_VERSION_HEADER_V2 "#mini_sql_schema_v2"
#define SCHEMA_VERSION_HEADER_V3 "#mini_sql_schema_v3"
#define SCHEMA_VERSION_HEADER_V4 "#mini_sql_schema_v4"
#define DEFAULT_COLUMN_TYPE "TEXT"

static bool ensure_storage_layout_buffers(CatalogSchema *schema, ErrorContext *err) {
    size_t *new_storage_slots;
    size_t *new_logical_indexes;

    new_storage_slots = realloc(schema->storage_slots, schema->column_count * sizeof(*new_storage_slots));
    if (new_storage_slots == NULL && schema->column_count > 0U) {
        set_error(err, "스키마 슬롯 정보를 준비하는 중 메모리가 부족합니다");
        return false;
    }
    schema->storage_slots = new_storage_slots;

    new_logical_indexes = realloc(schema->logical_indexes_by_storage_slot,
                                  schema->column_count * sizeof(*new_logical_indexes));
    if (new_logical_indexes == NULL && schema->column_count > 0U) {
        set_error(err, "스키마 슬롯 정보를 준비하는 중 메모리가 부족합니다");
        return false;
    }

    schema->logical_indexes_by_storage_slot = new_logical_indexes;
    return true;
}

static int storage_layout_priority(const char *type) {
    if (strings_equal_ci(type, "INT") || strings_equal_ci(type, "INTEGER")) {
        return 0;
    }
    if (strings_equal_ci(type, "CHAR")) {
        return 1;
    }
    if (strings_equal_ci(type, "VARCHAR") || strings_equal_ci(type, "TEXT")) {
        return 2;
    }

    return 3;
}

static bool validate_loaded_storage_layout(CatalogSchema *schema, ErrorContext *err) {
    bool *used_slots;
    size_t logical_index;

    if (!ensure_storage_layout_buffers(schema, err)) {
        return false;
    }

    used_slots = calloc(schema->column_count, sizeof(*used_slots));
    if (used_slots == NULL && schema->column_count > 0U) {
        set_error(err, "스키마 슬롯 정보를 검증하는 중 메모리가 부족합니다");
        return false;
    }

    for (logical_index = 0; logical_index < schema->column_count; ++logical_index) {
        size_t storage_slot = schema->storage_slots[logical_index];

        if (storage_slot >= schema->column_count) {
            free(used_slots);
            set_error(err, "스키마 저장 슬롯 정보가 잘못되었습니다");
            return false;
        }
        if (used_slots[storage_slot]) {
            free(used_slots);
            set_error(err, "스키마 저장 슬롯 정보가 중복되었습니다");
            return false;
        }

        used_slots[storage_slot] = true;
        schema->logical_indexes_by_storage_slot[storage_slot] = logical_index;
    }

    free(used_slots);
    return true;
}

/* src 의 컬럼/타입을 dst 로 깊은 복사한다. 실패 시 dst 는 비어 있는 상태로 남는다. */
bool catalog_copy_schema(const CatalogSchema *src, CatalogSchema *dst, ErrorContext *err) {
    size_t i;

    dst->columns = NULL;
    dst->types   = NULL;
    dst->max_lengths = NULL;
    dst->is_primary_keys = NULL;
    dst->storage_slots = NULL;
    dst->logical_indexes_by_storage_slot = NULL;
    dst->primary_key_index = -1;
    dst->column_count = 0U;

    if (src->column_count == 0U) {
        return true;
    }

    dst->columns = calloc(src->column_count, sizeof(*dst->columns));
    dst->types   = calloc(src->column_count, sizeof(*dst->types));
    dst->max_lengths = calloc(src->column_count, sizeof(*dst->max_lengths));
    dst->is_primary_keys = calloc(src->column_count, sizeof(*dst->is_primary_keys));
    dst->storage_slots = calloc(src->column_count, sizeof(*dst->storage_slots));
    dst->logical_indexes_by_storage_slot = calloc(src->column_count, sizeof(*dst->logical_indexes_by_storage_slot));
    if (dst->columns == NULL || dst->types == NULL ||
        dst->max_lengths == NULL || dst->is_primary_keys == NULL ||
        dst->storage_slots == NULL || dst->logical_indexes_by_storage_slot == NULL) {
        free(dst->columns);
        free(dst->types);
        free(dst->max_lengths);
        free(dst->is_primary_keys);
        free(dst->storage_slots);
        free(dst->logical_indexes_by_storage_slot);
        dst->columns = NULL;
        dst->types   = NULL;
        dst->max_lengths = NULL;
        dst->is_primary_keys = NULL;
        dst->storage_slots = NULL;
        dst->logical_indexes_by_storage_slot = NULL;
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
        dst->max_lengths[i] = src->max_lengths[i];
        dst->is_primary_keys[i] = src->is_primary_keys[i];
        dst->storage_slots[i] = src->storage_slots[i];
        if (src->is_primary_keys[i]) {
            dst->primary_key_index = (int) i;
        }
        dst->column_count += 1U;
    }

    for (i = 0; i < src->column_count; ++i) {
        dst->logical_indexes_by_storage_slot[i] = src->logical_indexes_by_storage_slot[i];
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
    free(schema->max_lengths);
    free(schema->is_primary_keys);
    free(schema->storage_slots);
    free(schema->logical_indexes_by_storage_slot);
    schema->columns = NULL;
    schema->types = NULL;
    schema->max_lengths = NULL;
    schema->is_primary_keys = NULL;
    schema->storage_slots = NULL;
    schema->logical_indexes_by_storage_slot = NULL;
    schema->primary_key_index = -1;
    schema->column_count = 0U;
}

/* schema 에 컬럼 이름/타입 한 쌍을 추가한다. */
static bool append_table_schema_entry(CatalogSchema *schema, const char *name, const char *type,
                                      size_t max_length, bool is_primary_key, ErrorContext *err) {
    char **new_columns;
    char **new_types;
    size_t *new_max_lengths;
    bool *new_primary_keys;
    size_t *new_storage_slots;
    size_t *new_logical_indexes;
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

    new_max_lengths = realloc(schema->max_lengths, (schema->column_count + 1U) * sizeof(*new_max_lengths));
    if (new_max_lengths == NULL) {
        set_error(err, "스키마를 만드는 중 메모리가 부족합니다");
        return false;
    }
    schema->max_lengths = new_max_lengths;

    new_primary_keys = realloc(schema->is_primary_keys, (schema->column_count + 1U) * sizeof(*new_primary_keys));
    if (new_primary_keys == NULL) {
        set_error(err, "스키마를 만드는 중 메모리가 부족합니다");
        return false;
    }
    schema->is_primary_keys = new_primary_keys;

    new_storage_slots = realloc(schema->storage_slots, (schema->column_count + 1U) * sizeof(*new_storage_slots));
    if (new_storage_slots == NULL) {
        set_error(err, "스키마를 만드는 중 메모리가 부족합니다");
        return false;
    }
    schema->storage_slots = new_storage_slots;

    new_logical_indexes = realloc(schema->logical_indexes_by_storage_slot,
                                  (schema->column_count + 1U) * sizeof(*new_logical_indexes));
    if (new_logical_indexes == NULL) {
        set_error(err, "스키마를 만드는 중 메모리가 부족합니다");
        return false;
    }
    schema->logical_indexes_by_storage_slot = new_logical_indexes;

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
    schema->max_lengths[schema->column_count] = max_length;
    schema->is_primary_keys[schema->column_count] = is_primary_key;
    schema->storage_slots[schema->column_count] = schema->column_count;
    schema->logical_indexes_by_storage_slot[schema->column_count] = schema->column_count;
    if (is_primary_key) {
        schema->primary_key_index = (int) schema->column_count;
    }
    schema->column_count += 1U;
    return true;
}

bool catalog_assign_storage_layout(CatalogSchema *schema, ErrorContext *err) {
    size_t next_slot = 0U;
    int priority;
    size_t logical_index;

    if (!ensure_storage_layout_buffers(schema, err)) {
        return false;
    }

    /*
     * 최소 구현에서는 사용자 스키마 순서는 그대로 두고,
     * 내부 저장 슬롯만 "고정 폭에 가까운 타입 우선"으로 다시 배치한다.
     * 현재 저장 포맷은 CSV지만, 향후 binary row 포맷으로 바꿔도 같은 메타데이터를 재사용할 수 있다.
     */
    for (priority = 0; priority <= 3; ++priority) {
        for (logical_index = 0; logical_index < schema->column_count; ++logical_index) {
            if (storage_layout_priority(schema->types[logical_index]) != priority) {
                continue;
            }

            schema->storage_slots[logical_index] = next_slot;
            schema->logical_indexes_by_storage_slot[next_slot] = logical_index;
            next_slot += 1U;
        }
    }

    return true;
}

size_t catalog_schema_storage_slot(const CatalogSchema *schema, size_t logical_index) {
    if (schema == NULL || logical_index >= schema->column_count || schema->storage_slots == NULL) {
        return logical_index;
    }

    return schema->storage_slots[logical_index];
}

size_t catalog_schema_logical_index(const CatalogSchema *schema, size_t storage_slot) {
    if (schema == NULL || storage_slot >= schema->column_count ||
        schema->logical_indexes_by_storage_slot == NULL) {
        return storage_slot;
    }

    return schema->logical_indexes_by_storage_slot[storage_slot];
}

static bool build_reordered_row(const CatalogSchema *schema, char **source_fields, size_t field_count,
                                bool to_storage_order, char ***out_fields, ErrorContext *err) {
    char **reordered_fields;
    size_t index;

    if (field_count != schema->column_count) {
        set_error(err, "행 필드 개수와 스키마 컬럼 개수가 다릅니다");
        return false;
    }

    reordered_fields = calloc(field_count, sizeof(*reordered_fields));
    if (reordered_fields == NULL) {
        set_error(err, "행 순서를 변환하는 중 메모리가 부족합니다");
        return false;
    }

    for (index = 0; index < schema->column_count; ++index) {
        size_t logical_index = to_storage_order ? index : catalog_schema_logical_index(schema, index);
        size_t storage_slot = catalog_schema_storage_slot(schema, logical_index);
        size_t source_index = to_storage_order ? logical_index : storage_slot;
        size_t target_index = to_storage_order ? storage_slot : logical_index;

        reordered_fields[target_index] = msql_strdup(source_fields[source_index]);
        if (reordered_fields[target_index] == NULL) {
            free_string_array(reordered_fields, field_count);
            set_error(err, "행 순서를 변환하는 중 메모리가 부족합니다");
            return false;
        }
    }

    *out_fields = reordered_fields;
    return true;
}

bool catalog_build_storage_row(const CatalogSchema *schema, char **logical_fields, size_t field_count,
                               char ***out_storage_fields, ErrorContext *err) {
    return build_reordered_row(schema, logical_fields, field_count, true, out_storage_fields, err);
}

bool catalog_build_logical_row(const CatalogSchema *schema, char **storage_fields, size_t field_count,
                               char ***out_logical_fields, ErrorContext *err) {
    return build_reordered_row(schema, storage_fields, field_count, false, out_logical_fields, err);
}

/*
 * 현재 프로젝트의 .schema 포맷으로 스키마를 저장한다.
 * 첫 줄은 버전 헤더, 이후 줄은 "컬럼명,타입,길이,PK여부,저장슬롯" CSV 한 줄씩이다.
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

    if (fprintf(file, "%s\n", SCHEMA_VERSION_HEADER_V4) < 0) {
        set_error(err, "스키마 헤더를 쓰지 못했습니다");
        fclose(file);
        free(path);
        return false;
    }

    for (i = 0; i < schema->column_count; ++i) {
        char *row_fields[5];
        char length_buffer[32];
        char slot_buffer[32];

        row_fields[0] = schema->columns[i];
        row_fields[1] = schema->types[i];
        snprintf(length_buffer, sizeof(length_buffer), "%zu", schema->max_lengths[i]);
        row_fields[2] = length_buffer;
        row_fields[3] = schema->is_primary_keys[i] ? "1" : "0";
        snprintf(slot_buffer, sizeof(slot_buffer), "%zu", catalog_schema_storage_slot(schema, i));
        row_fields[4] = slot_buffer;
        if (!write_csv_row(file, row_fields, 5U, err)) {
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
    bool v3_or_v4_format = false;
    bool has_storage_slot = false;

    schema->columns = NULL;
    schema->types = NULL;
    schema->max_lengths = NULL;
    schema->is_primary_keys = NULL;
    schema->storage_slots = NULL;
    schema->logical_indexes_by_storage_slot = NULL;
    schema->primary_key_index = -1;
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

    /* 새 포맷이면 헤더 다음 줄부터 "컬럼명,타입,길이,PK여부,저장슬롯"을 읽는다. */
    if (strings_equal_ci(trim_in_place(line), SCHEMA_VERSION_HEADER_V4) ||
        strings_equal_ci(trim_in_place(line), SCHEMA_VERSION_HEADER_V3) ||
        strings_equal_ci(trim_in_place(line), SCHEMA_VERSION_HEADER_V2)) {
        new_format = true;
        v3_or_v4_format = strings_equal_ci(trim_in_place(line), SCHEMA_VERSION_HEADER_V4) ||
                          strings_equal_ci(trim_in_place(line), SCHEMA_VERSION_HEADER_V3);
        has_storage_slot = strings_equal_ci(trim_in_place(line), SCHEMA_VERSION_HEADER_V4);
        line = strtok(NULL, "\n");
        while (line != NULL) {
            char *trimmed = trim_in_place(line);
            char **fields = NULL;
            size_t field_count = 0U;
            bool ok;
            size_t max_length = 0U;
            bool is_primary_key = false;
            size_t storage_slot = schema->column_count;

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

            if (v3_or_v4_format && field_count > 2U) {
                char *length_text = trim_in_place(fields[2]);
                char *end = NULL;
                unsigned long long length_value = strtoull(length_text, &end, 10);

                if (end == NULL || *end != '\0') {
                    free(contents);
                    free_string_array(fields, field_count);
                    catalog_free_schema(schema);
                    set_error(err, "테이블 %s의 스키마 길이 정보가 잘못되었습니다", table_name);
                    return false;
                }
                max_length = (size_t) length_value;
            }
            if (v3_or_v4_format && field_count > 3U) {
                char *primary_text = trim_in_place(fields[3]);
                is_primary_key = strings_equal_ci(primary_text, "1") || strings_equal_ci(primary_text, "true");
            }
            if (has_storage_slot && field_count > 4U) {
                char *slot_text = trim_in_place(fields[4]);
                char *end = NULL;
                unsigned long long slot_value = strtoull(slot_text, &end, 10);

                if (end == NULL || *end != '\0') {
                    free(contents);
                    free_string_array(fields, field_count);
                    catalog_free_schema(schema);
                    set_error(err, "테이블 %s의 스키마 저장 슬롯 정보가 잘못되었습니다", table_name);
                    return false;
                }
                storage_slot = (size_t) slot_value;
            }

            ok = append_table_schema_entry(schema, trim_in_place(fields[0]),
                                           field_count > 1U ? trim_in_place(fields[1]) : DEFAULT_COLUMN_TYPE,
                                           max_length,
                                           is_primary_key,
                                           err);
            free_string_array(fields, field_count);
            if (!ok) {
                free(contents);
                catalog_free_schema(schema);
                return false;
            }
            if (has_storage_slot) {
                schema->storage_slots[schema->column_count - 1U] = storage_slot;
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
                                           0U,
                                           false,
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

    if (has_storage_slot && !validate_loaded_storage_layout(schema, err)) {
        catalog_free_schema(schema);
        return false;
    }

    return true;
}
