#include "executor/statements/sql_statement_handlers.h"

#include "catalog/schema_catalog.h"
#include "executor/statements/sql_statement_support.h"
#include "storage/storage_engine.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct PreparedInsertRows {
    char ***items;
    size_t row_count;
    size_t field_count;
} PreparedInsertRows;

typedef struct PrimaryKeyLookupState {
    const CatalogSchema *schema;
    const char *table_name;
    const char *primary_key_value;
    bool found_duplicate;
} PrimaryKeyLookupState;

static void free_prepared_insert_rows(PreparedInsertRows *prepared_rows) {
    size_t i;

    for (i = 0; i < prepared_rows->row_count; ++i) {
        free_string_array(prepared_rows->items[i], prepared_rows->field_count);
    }
    free(prepared_rows->items);
    prepared_rows->items = NULL;
    prepared_rows->row_count = 0U;
    prepared_rows->field_count = 0U;
}

static bool append_prepared_insert_row(PreparedInsertRows *prepared_rows, char **row_values,
                                       ErrorContext *err) {
    char ***new_items = realloc(prepared_rows->items, (prepared_rows->row_count + 1U) * sizeof(*new_items));

    if (new_items == NULL) {
        free_string_array(row_values, prepared_rows->field_count);
        set_error(err, "INSERT 실행 준비 중 메모리가 부족합니다");
        return false;
    }

    prepared_rows->items = new_items;
    prepared_rows->items[prepared_rows->row_count] = row_values;
    prepared_rows->row_count += 1U;
    return true;
}

static bool is_integer_value(const char *value) {
    const unsigned char *cursor = (const unsigned char *) value;

    if (*cursor == '+' || *cursor == '-') {
        cursor += 1U;
    }
    if (*cursor == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (!isdigit(*cursor)) {
            return false;
        }
        cursor += 1U;
    }
    return true;
}

static bool validate_insert_value(const CatalogSchema *schema, size_t column_index, const char *value,
                                  ErrorContext *err) {
    const char *type = schema->types[column_index];
    size_t max_length = schema->max_lengths[column_index];

    if (schema->is_primary_keys[column_index] && value[0] == '\0') {
        set_error(err, "PRIMARY KEY 컬럼에는 빈 값을 넣을 수 없습니다: %s", schema->columns[column_index]);
        return false;
    }
    if ((strings_equal_ci(type, "INT") || strings_equal_ci(type, "INTEGER")) &&
        !is_integer_value(value)) {
        set_error(err, "정수 컬럼에는 숫자만 넣을 수 있습니다: %s", schema->columns[column_index]);
        return false;
    }
    if (max_length > 0U && strlen(value) > max_length) {
        set_error(err, "문자열 길이 제한을 초과했습니다: %s (최대 %zu자)",
                  schema->columns[column_index], max_length);
        return false;
    }

    return true;
}

static bool prepare_insert_row(const InsertStatement *statement, const CatalogSchema *schema,
                               size_t row_index, char ***out_row_values, ErrorContext *err) {
    char **row_values = calloc(schema->column_count, sizeof(*row_values));
    bool *assigned = NULL;
    size_t input_count;
    size_t i;

    if (row_values == NULL) {
        set_error(err, "INSERT 실행 준비 중 메모리가 부족합니다");
        return false;
    }

    for (i = 0; i < schema->column_count; ++i) {
        row_values[i] = msql_strdup("");
        if (row_values[i] == NULL) {
            free_string_array(row_values, schema->column_count);
            set_error(err, "INSERT 실행 준비 중 메모리가 부족합니다");
            return false;
        }
    }

    input_count = statement->column_count == 0U ? schema->column_count : statement->column_count;
    if (statement->value_count != input_count) {
        free_string_array(row_values, schema->column_count);
        set_error(err, "INSERT 값 개수가 스키마 컬럼 개수와 다릅니다");
        return false;
    }

    assigned = calloc(schema->column_count, sizeof(*assigned));
    if (assigned == NULL) {
        free_string_array(row_values, schema->column_count);
        set_error(err, "INSERT 실행 준비 중 메모리가 부족합니다");
        return false;
    }

    for (i = 0; i < input_count; ++i) {
        const char *column_name = statement->column_count == 0U ? schema->columns[i] : statement->columns[i];
        const char *input_value = statement->values[row_index * statement->value_count + i];
        int index = statement->column_count == 0U ? (int) i
                                                  : find_column_index(schema->columns, schema->column_count, column_name);

        if (index < 0) {
            free_string_array(row_values, schema->column_count);
            free(assigned);
            set_error(err, "INSERT에 없는 컬럼이 사용되었습니다: %s", column_name);
            return false;
        }
        if (assigned[index]) {
            free_string_array(row_values, schema->column_count);
            free(assigned);
            set_error(err, "INSERT에 같은 컬럼이 중복으로 들어왔습니다: %s", column_name);
            return false;
        }

        free(row_values[index]);
        row_values[index] = msql_strdup(input_value);
        if (row_values[index] == NULL) {
            free_string_array(row_values, schema->column_count);
            free(assigned);
            set_error(err, "INSERT 실행 준비 중 메모리가 부족합니다");
            return false;
        }

        assigned[index] = true;
    }

    free(assigned);
    *out_row_values = row_values;
    return true;
}

static bool build_prepared_insert_rows(const InsertStatement *statement, const CatalogSchema *schema,
                                       PreparedInsertRows *prepared_rows, ErrorContext *err) {
    size_t row_index;

    prepared_rows->items = NULL;
    prepared_rows->row_count = 0U;
    prepared_rows->field_count = schema->column_count;

    for (row_index = 0; row_index < statement->row_count; ++row_index) {
        char **row_values = NULL;

        if (!prepare_insert_row(statement, schema, row_index, &row_values, err)) {
            free_prepared_insert_rows(prepared_rows);
            return false;
        }
        if (!append_prepared_insert_row(prepared_rows, row_values, err)) {
            free_prepared_insert_rows(prepared_rows);
            return false;
        }
    }

    return true;
}

static bool validate_prepared_insert_rows(const CatalogSchema *schema, const PreparedInsertRows *prepared_rows,
                                          ErrorContext *err) {
    size_t row_index;
    size_t column_index;

    for (row_index = 0; row_index < prepared_rows->row_count; ++row_index) {
        for (column_index = 0; column_index < prepared_rows->field_count; ++column_index) {
            if (!validate_insert_value(schema, column_index,
                                       prepared_rows->items[row_index][column_index], err)) {
                return false;
            }
        }
    }

    return true;
}

static bool validate_primary_key_batch_duplicates(const CatalogSchema *schema,
                                                  const PreparedInsertRows *prepared_rows,
                                                  ErrorContext *err) {
    size_t left_index;
    size_t right_index;
    int primary_key_index = schema->primary_key_index;

    if (primary_key_index < 0) {
        return true;
    }

    for (left_index = 0; left_index < prepared_rows->row_count; ++left_index) {
        for (right_index = left_index + 1U; right_index < prepared_rows->row_count; ++right_index) {
            if (strcmp(prepared_rows->items[left_index][primary_key_index],
                       prepared_rows->items[right_index][primary_key_index]) == 0) {
                set_error(err, "PRIMARY KEY 값이 같은 행이 한 INSERT 안에 중복되었습니다: %s",
                          prepared_rows->items[left_index][primary_key_index]);
                return false;
            }
        }
    }

    return true;
}

static bool find_duplicate_primary_key(char **fields, size_t field_count, void *user_data,
                                       ErrorContext *err) {
    PrimaryKeyLookupState *lookup_state = (PrimaryKeyLookupState *) user_data;
    int primary_key_index = lookup_state->schema->primary_key_index;

    if (!sql_executor_validate_row_field_count(lookup_state->schema, lookup_state->table_name,
                                               field_count, err)) {
        return false;
    }
    if (strcmp(fields[primary_key_index], lookup_state->primary_key_value) == 0) {
        lookup_state->found_duplicate = true;
    }
    return true;
}

static bool validate_primary_key_storage_duplicates(const CatalogSchema *schema,
                                                    const PreparedInsertRows *prepared_rows,
                                                    const InsertStatement *statement,
                                                    const ExecutionContext *context,
                                                    ErrorContext *err) {
    size_t row_index;
    int primary_key_index = schema->primary_key_index;

    if (primary_key_index < 0) {
        return true;
    }

    for (row_index = 0; row_index < prepared_rows->row_count; ++row_index) {
        PrimaryKeyLookupState lookup_state;

        lookup_state.schema = schema;
        lookup_state.table_name = statement->table_name;
        lookup_state.primary_key_value = prepared_rows->items[row_index][primary_key_index];
        lookup_state.found_duplicate = false;

        if (!storage_engine_scan_rows(context->storage_engine, statement->table_name,
                                      find_duplicate_primary_key, &lookup_state, err)) {
            return false;
        }
        if (lookup_state.found_duplicate) {
            set_error(err, "PRIMARY KEY 값이 이미 존재합니다: %s", lookup_state.primary_key_value);
            return false;
        }
    }

    return true;
}

static bool append_prepared_rows(const PreparedInsertRows *prepared_rows, const InsertStatement *statement,
                                 const ExecutionContext *context, ErrorContext *err) {
    size_t row_index;

    for (row_index = 0; row_index < prepared_rows->row_count; ++row_index) {
        if (!storage_engine_append_row(context->storage_engine, statement->table_name,
                                       prepared_rows->items[row_index], prepared_rows->field_count, err)) {
            return false;
        }
    }

    return true;
}

bool execute_insert_statement(const InsertStatement *statement, const ExecutionContext *context,
                              ErrorContext *err) {
    CatalogSchema schema = {0};
    PreparedInsertRows prepared_rows = {0};
    bool ok = false;

    if (!sql_executor_load_schema(context, statement->table_name, &schema, err)) {
        return false;
    }
    if (!build_prepared_insert_rows(statement, &schema, &prepared_rows, err)) {
        goto cleanup;
    }
    if (!validate_prepared_insert_rows(&schema, &prepared_rows, err)) {
        goto cleanup;
    }
    if (!validate_primary_key_batch_duplicates(&schema, &prepared_rows, err)) {
        goto cleanup;
    }
    if (!validate_primary_key_storage_duplicates(&schema, &prepared_rows, statement, context, err)) {
        goto cleanup;
    }
    if (!append_prepared_rows(&prepared_rows, statement, context, err)) {
        goto cleanup;
    }

    fprintf(sql_executor_output(context), "INSERT %zu\n", prepared_rows.row_count);
    ok = true;

cleanup:
    free_prepared_insert_rows(&prepared_rows);
    catalog_free_schema(&schema);
    return ok;
}
