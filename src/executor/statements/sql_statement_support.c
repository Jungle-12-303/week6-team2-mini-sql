#include "executor/statements/sql_statement_support.h"

#include "storage/storage_engine.h"

/*
 * statement 파일들이 공통으로 쓰는 얇은 지원 함수들이다.
 * output 선택과 WHERE 컬럼 인덱스 계산처럼 여러 문장에서 반복되는 로직만 둔다.
 */

FILE *sql_executor_output(const ExecutionContext *context) {
    return context->output != NULL ? context->output : stdout;
}

void sql_executor_print_status(const ExecutionContext *context, const char *message) {
    fprintf(sql_executor_output(context), "%s\n", message);
}

bool sql_executor_load_schema(const ExecutionContext *context, const char *table_name,
                              CatalogSchema *schema, ErrorContext *err) {
    return storage_engine_load_schema(context->storage_engine, table_name, schema, err);
}

bool sql_executor_validate_row_field_count(const CatalogSchema *schema, const char *table_name,
                                           size_t field_count, ErrorContext *err) {
    if (field_count == schema->column_count) {
        return true;
    }

    set_error(err, "테이블 '%s'의 데이터 행이 손상되었습니다. 예상 컬럼 수는 %zu개인데 실제로는 %zu개입니다",
              table_name, schema->column_count, field_count);
    return false;
}

int sql_executor_resolve_where_index(const CatalogSchema *schema, const WhereClause *where,
                                     ErrorContext *err) {
    int index;

    if (!where->has_where) {
        return -1;
    }

    index = find_column_index(schema->columns, schema->column_count, where->where_column);
    if (index < 0) {
        set_error(err, "WHERE 절에 없는 컬럼이 사용되었습니다: %s", where->where_column);
    }

    return index;
}
