#include "mini_sql.h"

#include <stdlib.h>
#include <string.h>

/*
 * Parser 는 TokenList 위를 앞에서부터 이동하면서 AST 를 만든다.
 * current 는 "지금 읽고 있는 토큰 위치"를 가리키고,
 * 각 parse_* 함수는 성공 시 current 를 다음 위치로 전진시킨다.
 */
typedef struct Parser {
    const TokenList *tokens;
    size_t current;
    ErrorContext *err;
} Parser;

typedef bool (*StatementParserFn)(Parser *parser, Statement *out_statement);

static bool parse_insert_statement(Parser *parser, Statement *out_statement);
static bool parse_select_statement(Parser *parser, Statement *out_statement);
static bool parse_create_table_statement(Parser *parser, Statement *out_statement);
static bool parse_drop_table_statement(Parser *parser, Statement *out_statement);
static bool parse_delete_statement(Parser *parser, Statement *out_statement);

static const char *const TOKEN_NAMES[] = {
    [TOKEN_IDENTIFIER] = "식별자",
    [TOKEN_NUMBER] = "숫자",
    [TOKEN_STRING] = "문자열",
    [TOKEN_INSERT] = "INSERT",
    [TOKEN_INTO] = "INTO",
    [TOKEN_VALUES] = "VALUES",
    [TOKEN_SELECT] = "SELECT",
    [TOKEN_FROM] = "FROM",
    [TOKEN_WHERE] = "WHERE",
    [TOKEN_CREATE] = "CREATE",
    [TOKEN_TABLE] = "TABLE",
    [TOKEN_DROP] = "DROP",
    [TOKEN_DELETE] = "DELETE",
    [TOKEN_STAR] = "*",
    [TOKEN_COMMA] = ",",
    [TOKEN_LPAREN] = "(",
    [TOKEN_RPAREN] = ")",
    [TOKEN_SEMICOLON] = ";",
    [TOKEN_EQUALS] = "=",
    [TOKEN_DOT] = ".",
    [TOKEN_EOF] = "입력 끝"
};

static const StatementParserFn STATEMENT_PARSERS[] = {
    [TOKEN_INSERT] = parse_insert_statement,
    [TOKEN_SELECT] = parse_select_statement,
    [TOKEN_CREATE] = parse_create_table_statement,
    [TOKEN_DROP] = parse_drop_table_statement,
    [TOKEN_DELETE] = parse_delete_statement
};

/* 현재 가리키는 토큰을 본다. 위치는 움직이지 않는다. */
static const Token *peek(Parser *parser) {
    return &parser->tokens->items[parser->current];
}

static const Token *previous(Parser *parser) {
    /* 직전에 소비한 토큰을 반환한다. */
    return &parser->tokens->items[parser->current - 1U];
}

/* 현재 토큰이 EOF 인지 확인한다. */
static bool is_at_end(Parser *parser) {
    return peek(parser)->type == TOKEN_EOF;
}

/* 토큰을 하나 소비하고 이전 토큰을 반환한다. */
static const Token *advance(Parser *parser) {
    if (!is_at_end(parser)) {
        parser->current += 1U;
    }
    return previous(parser);
}

/* 현재 토큰이 기대한 종류인지 확인만 한다. */
static bool check(Parser *parser, TokenType type) {
    if (is_at_end(parser)) {
        return type == TOKEN_EOF;
    }
    return peek(parser)->type == type;
}

/* 현재 토큰이 기대한 종류면 소비하고 true, 아니면 false 를 반환한다. */
static bool match(Parser *parser, TokenType type) {
    if (!check(parser, type)) {
        return false;
    }
    advance(parser);
    return true;
}

/* 에러 메시지에서 사람이 이해하기 쉬운 토큰 이름을 만든다. */
static const char *token_name(TokenType type) {
    if ((size_t) type < (sizeof(TOKEN_NAMES) / sizeof(TOKEN_NAMES[0])) && TOKEN_NAMES[type] != NULL) {
        return TOKEN_NAMES[type];
    }

    return "토큰";
}

/* 현재 위치 정보를 포함한 파싱 에러 메시지를 만든다. */
static bool parse_error(Parser *parser, const char *message) {
    const Token *token = peek(parser);

    set_error(parser->err, "%s (%d:%d, 주변 토큰: %s)", message, token->line, token->column,
              token_name(token->type));
    return false;
}

/* StatementList 의 크기를 늘려 새 statement 를 넣을 준비를 한다. */
static bool ensure_statement_capacity(StatementList *statements, ErrorContext *err) {
    /* 새 AST 배열 포인터를 담을 변수다. */
    Statement *new_items;
    /* 새 AST 배열 용량이다. */
    size_t new_capacity;

    /* 아직 빈 자리가 있으면 재할당할 필요가 없다. */
    if (statements->count < statements->capacity) {
        return true;
    }

    /* 비어 있으면 8개로 시작하고, 이후에는 2배씩 늘린다. */
    new_capacity = statements->capacity == 0U ? 8U : statements->capacity * 2U;
    /* 더 큰 statement 배열을 확보한다. */
    new_items = realloc(statements->items, new_capacity * sizeof(*new_items));
    /* 재할당에 실패하면 더 진행할 수 없다. */
    if (new_items == NULL) {
        set_error(err, "SQL 파싱 중 메모리가 부족합니다");
        return false;
    }

    /* 새 배열 포인터를 저장한다. */
    statements->items = new_items;
    /* 새 용량을 저장한다. */
    statements->capacity = new_capacity;
    /* 용량 확보 성공을 반환한다. */
    return true;
}

/* 완성된 statement 하나를 StatementList 뒤에 붙인다. */
static bool append_statement(StatementList *statements, Statement statement, ErrorContext *err) {
    /* 새 statement 가 들어갈 자리가 있는지 먼저 확인한다. */
    if (!ensure_statement_capacity(statements, err)) {
        return false;
    }

    /* 완성된 AST 문장을 배열 뒤에 저장한다. */
    statements->items[statements->count] = statement;
    /* 문장 개수를 하나 늘린다. */
    statements->count += 1U;
    /* 추가 성공을 반환한다. */
    return true;
}

static void free_create_table_statement_contents(CreateTableStatement *statement) {
    /* 테이블 이름 문자열을 해제한다. */
    free(statement->table_name);
    /* 컬럼 이름 배열 전체를 해제한다. */
    free_string_array(statement->columns, statement->column_count);
    /* 컬럼 타입 배열 전체를 해제한다. */
    free_string_array(statement->column_types, statement->column_count);
}

/* statement 하나가 내부적으로 소유한 문자열/배열 메모리를 해제한다. */
static void free_statement_contents(Statement *statement) {
    if (statement->type == STATEMENT_INSERT) {
        free(statement->as.insert_stmt.table_name);
        free_string_array(statement->as.insert_stmt.columns, statement->as.insert_stmt.column_count);
        free_string_array(statement->as.insert_stmt.values, statement->as.insert_stmt.value_count);
    } else if (statement->type == STATEMENT_SELECT) {
        free_string_array(statement->as.select_stmt.columns, statement->as.select_stmt.column_count);
        free(statement->as.select_stmt.table_name);
        free(statement->as.select_stmt.where.where_column);
        free(statement->as.select_stmt.where.where_value);
    } else if (statement->type == STATEMENT_CREATE_TABLE) {
        free_create_table_statement_contents(&statement->as.create_table_stmt);
    } else if (statement->type == STATEMENT_DROP_TABLE) {
        free(statement->as.drop_table_stmt.table_name);
    } else if (statement->type == STATEMENT_DELETE) {
        free(statement->as.delete_stmt.table_name);
        free(statement->as.delete_stmt.where.where_column);
        free(statement->as.delete_stmt.where.where_value);
    }
}

/* 현재 토큰이 특정 타입이어야만 다음 단계로 진행할 수 있을 때 쓰는 보조 함수다. */
static bool consume(Parser *parser, TokenType type, const char *message) {
    /* 현재 토큰이 기대한 타입이면 그 토큰을 소비한다. */
    if (check(parser, type)) {
        advance(parser);
        return true;
    }
    /* 타입이 다르면 위치 정보가 들어간 파싱 오류를 만든다. */
    return parse_error(parser, message);
}

/* 식별자 토큰 하나를 읽어서 heap 문자열로 복사한다. */
static bool parse_identifier(Parser *parser, char **out_name) {
    /* 직전에 소비한 식별자 토큰을 가리킬 포인터다. */
    const Token *token;

    /* 현재 위치에 식별자가 반드시 있어야 한다. */
    if (!consume(parser, TOKEN_IDENTIFIER, "식별자가 필요합니다")) {
        return false;
    }

    /* 방금 소비한 식별자 토큰을 가져온다. */
    token = previous(parser);
    /* 토큰 텍스트를 새 문자열로 복사해 호출자에게 넘긴다. */
    *out_name = msql_strdup(token->text);
    /* 복사 메모리 할당에 실패하면 중단한다. */
    if (*out_name == NULL) {
        set_error(parser->err, "SQL 파싱 중 메모리가 부족합니다");
        return false;
    }

    /* 식별자 파싱 성공을 반환한다. */
    return true;
}

/*
 * schema.table 처럼 점으로 연결된 이름을 하나의 문자열로 합친다.
 * 파일 계층에서는 이 이름을 다시 경로로 바꿔 사용한다.
 */
static bool parse_qualified_name(Parser *parser, char **out_name) {
    /* 첫 번째 이름 조각 또는 누적된 전체 이름이다. */
    char *name = NULL;

    /* 맨 앞 식별자를 먼저 읽는다. */
    if (!parse_identifier(parser, &name)) {
        return false;
    }

    /* 점(.)이 이어지는 동안 schema.table 형식 이름을 계속 합친다. */
    while (match(parser, TOKEN_DOT)) {
        /* 점 뒤 식별자 한 조각을 담을 문자열이다. */
        char *part = NULL;
        /* 이전 이름과 새 조각을 합친 결과 문자열이다. */
        char *combined;
        /* 합쳐진 이름의 총 길이다. */
        size_t combined_length;

        /* 점 뒤에는 식별자가 반드시 와야 한다. */
        if (!parse_identifier(parser, &part)) {
            free(name);
            return false;
        }

        /* "기존이름.새조각" 길이를 계산한다. */
        combined_length = strlen(name) + 1U + strlen(part);
        /* 합쳐진 이름을 담을 새 버퍼를 할당한다. */
        combined = malloc(combined_length + 1U);
        /* 할당에 실패하면 기존 조각들을 모두 정리하고 중단한다. */
        if (combined == NULL) {
            free(name);
            free(part);
            set_error(parser->err, "SQL 파싱 중 메모리가 부족합니다");
            return false;
        }

        /* schema.table 형식 문자열을 실제로 만든다. */
        snprintf(combined, combined_length + 1U, "%s.%s", name, part);
        /* 이전 이름 문자열을 정리한다. */
        free(name);
        /* 새 조각 문자열도 정리한다. */
        free(part);
        /* 누적 이름을 새 문자열로 갱신한다. */
        name = combined;
    }

    /* 완성된 이름을 호출자에게 넘긴다. */
    *out_name = name;
    /* qualified name 파싱 성공을 반환한다. */
    return true;
}

/* WHERE 절 값, INSERT 값처럼 "리터럴 위치"에 올 수 있는 값을 읽는다. */
static bool parse_value(Parser *parser, char **out_value) {
    /* 현재 위치의 토큰을 본다. */
    const Token *token = peek(parser);

    /* 리터럴 위치에는 문자열, 숫자, 식별자만 허용한다. */
    if (token->type != TOKEN_STRING && token->type != TOKEN_NUMBER && token->type != TOKEN_IDENTIFIER) {
        return parse_error(parser, "리터럴 값이 필요합니다");
    }

    /* 토큰 텍스트를 새 문자열로 복사해 결과로 넘긴다. */
    *out_value = msql_strdup(token->text);
    /* 복사 메모리 할당에 실패하면 중단한다. */
    if (*out_value == NULL) {
        set_error(parser->err, "SQL 파싱 중 메모리가 부족합니다");
        return false;
    }

    /* 현재 리터럴 토큰을 소비한다. */
    advance(parser);
    /* 값 파싱 성공을 반환한다. */
    return true;
}

/*
 * 쉼표로 구분된 목록을 파싱하는 공통 헬퍼다.
 * parse_item 콜백만 교체하면 식별자 목록과 값 목록 모두 처리할 수 있다.
 */
typedef bool (*ItemParserFn)(Parser *parser, char **out_item);

static bool parse_comma_list(Parser *parser, ItemParserFn parse_item,
                             char ***out_items, size_t *out_count) {
    /* 최종 결과 문자열 배열이다. */
    char **items = NULL;
    /* 현재까지 읽은 원소 개수다. */
    size_t count = 0U;
    /* 동적 배열 용량이다. */
    size_t capacity = 0U;
    /* 새 원소 하나를 임시로 담는 포인터다. */
    char *item = NULL;

    /* 첫 번째 원소는 반드시 하나 읽어야 한다. */
    if (!parse_item(parser, &item)) {
        return false;
    }
    /* 읽은 원소의 소유권을 결과 배열로 넘긴다. */
    if (!msql_string_array_push_owned(&items, &count, &capacity, item, parser->err)) {
        return false;
    }

    /* 쉼표가 이어지는 동안 다음 원소를 계속 읽는다. */
    while (match(parser, TOKEN_COMMA)) {
        /* 쉼표 뒤 원소를 하나 읽는다. */
        if (!parse_item(parser, &item)) {
            free_string_array(items, count);
            return false;
        }
        /* 읽은 원소를 결과 배열 뒤에 붙인다. */
        if (!msql_string_array_push_owned(&items, &count, &capacity, item, parser->err)) {
            free_string_array(items, count);
            return false;
        }
    }

    /* 완성된 결과 배열을 호출자에게 넘긴다. */
    *out_items = items;
    /* 최종 원소 개수를 호출자에게 넘긴다. */
    *out_count = count;
    /* 목록 파싱 성공을 반환한다. */
    return true;
}

/* col1, col2, col3 처럼 쉼표로 구분된 식별자 목록을 읽는다. */
static bool parse_identifier_list(Parser *parser, char ***out_items, size_t *out_count) {
    return parse_comma_list(parser, parse_identifier, out_items, out_count);
}

/* VALUES (...) 안에 들어가는 값 목록을 읽는다. */
static bool parse_value_list(Parser *parser, char ***out_items, size_t *out_count) {
    return parse_comma_list(parser, parse_value, out_items, out_count);
}

/*
 * CREATE TABLE 의 "(col type, col type, ...)" 부분을 읽는다.
 * 타입이 생략되면 TEXT 로 취급한다.
 */
static bool parse_column_definition_list(Parser *parser, char ***out_columns, char ***out_types, size_t *out_count) {
    /* 컬럼 이름 배열이다. */
    char **columns = NULL;
    /* 컬럼 타입 배열이다. */
    char **types = NULL;
    /* 현재까지 읽은 컬럼 개수다. */
    size_t count = 0U;
    /* 새 컬럼 이름 하나를 담는 임시 포인터다. */
    char *col_name = NULL;
    /* 새 컬럼 타입 하나를 담는 임시 포인터다. */
    char *col_type = NULL;

    /* 쉼표가 끊길 때까지 컬럼 정의를 하나씩 읽는다. */
    while (true) {
        /* realloc 결과를 잠시 받을 임시 포인터다. */
        char **tmp;
        /* 새 반복을 시작하므로 임시 포인터를 비운다. */
        col_name = col_type = NULL;

        /* 컬럼 이름을 먼저 읽는다. */
        if (!parse_identifier(parser, &col_name)) {
            goto fail;
        }
        /* 컬럼 이름 뒤에 식별자가 또 있으면 그것을 타입 이름으로 본다. */
        if (check(parser, TOKEN_IDENTIFIER)) {
            if (!parse_identifier(parser, &col_type)) {
                goto fail;
            }
        } else {
            /* 타입이 생략되면 기본 TEXT 타입을 넣는다. */
            col_type = msql_strdup("TEXT");
            if (col_type == NULL) {
                set_error(parser->err, "SQL 파싱 중 메모리가 부족합니다");
                goto fail;
            }
        }

        /* 컬럼 이름 배열을 한 칸 늘린다. */
        tmp = realloc(columns, (count + 1U) * sizeof(*tmp));
        if (tmp == NULL) {
            set_error(parser->err, "SQL 파싱 중 메모리가 부족합니다");
            goto fail;
        }
        columns = tmp;

        /* 컬럼 타입 배열도 같은 크기로 한 칸 늘린다. */
        tmp = realloc(types, (count + 1U) * sizeof(*tmp));
        if (tmp == NULL) {
            set_error(parser->err, "SQL 파싱 중 메모리가 부족합니다");
            goto fail;
        }
        types = tmp;

        /* 새 컬럼 이름을 배열 끝에 넣는다. */
        columns[count] = col_name;
        /* 새 컬럼 타입을 배열 끝에 넣는다. */
        types[count]   = col_type;
        /* 소유권이 배열로 넘어갔으므로 임시 포인터를 비운다. */
        col_name = col_type = NULL;
        /* 컬럼 개수를 하나 늘린다. */
        count += 1U;

        /* 쉼표가 없으면 컬럼 정의 목록이 끝난 것이다. */
        if (!match(parser, TOKEN_COMMA)) {
            break;
        }
    }

    /* 완성된 컬럼 이름 배열을 결과로 넘긴다. */
    *out_columns = columns;
    /* 완성된 타입 배열을 결과로 넘긴다. */
    *out_types   = types;
    /* 최종 컬럼 개수를 결과로 넘긴다. */
    *out_count   = count;
    /* 컬럼 정의 목록 파싱 성공을 반환한다. */
    return true;

fail:
    /* 부분적으로 읽은 컬럼 이름을 정리한다. */
    free(col_name);
    /* 부분적으로 읽은 컬럼 타입을 정리한다. */
    free(col_type);
    /* 지금까지 모은 컬럼 이름 배열을 정리한다. */
    free_string_array(columns, count);
    /* 지금까지 모은 타입 배열을 정리한다. */
    free_string_array(types, count);
    /* 컬럼 정의 목록 파싱 실패를 반환한다. */
    return false;
}

/*
 * WHERE 절을 파싱해 WhereClause 를 채운다.
 * WHERE 키워드가 없으면 has_where = false 로 설정하고 정상 반환한다.
 * SELECT 와 DELETE 가 동일한 WHERE 구문을 공유하므로 여기서 한 번만 구현한다.
 */
static bool parse_where_clause(Parser *parser, WhereClause *out_where) {
    /* 현재 위치가 WHERE 가 아니면 WHERE 절이 없는 문장이다. */
    if (!match(parser, TOKEN_WHERE)) {
        out_where->has_where    = false;
        out_where->where_column = NULL;
        out_where->where_value  = NULL;
        return true;
    }

    /* 여기서부터는 실제 WHERE 절이 있다는 뜻이다. */
    out_where->has_where = true;
    /* 비교할 컬럼 이름을 읽는다. */
    if (!parse_identifier(parser, &out_where->where_column)) {
        return false;
    }
    /* WHERE 절은 반드시 '=' 비교 연산을 사용해야 한다. */
    if (!consume(parser, TOKEN_EQUALS, "WHERE 절에는 '='가 필요합니다")) {
        free(out_where->where_column);
        out_where->where_column = NULL;
        return false;
    }
    /* '=' 오른쪽 비교 값을 읽는다. */
    if (!parse_value(parser, &out_where->where_value)) {
        free(out_where->where_column);
        out_where->where_column = NULL;
        return false;
    }
    /* WHERE 절 파싱 성공을 반환한다. */
    return true;
}

/* INSERT INTO ... VALUES ... 문장 하나를 AST 로 만든다. */
static bool parse_insert_statement(Parser *parser, Statement *out_statement) {
    /* INSERT AST 를 담을 임시 statement 구조체다. */
    Statement stmt = {0};
    /* 이 문장이 INSERT 라는 사실을 먼저 기록한다. */
    stmt.type = STATEMENT_INSERT;

    /* INSERT 키워드를 소비한다. */
    if (!consume(parser, TOKEN_INSERT, "INSERT가 필요합니다")) goto fail;
    /* INSERT 뒤 INTO 키워드를 소비한다. */
    if (!consume(parser, TOKEN_INTO, "INSERT 뒤에는 INTO가 필요합니다")) goto fail;
    /* 대상 테이블 이름을 읽는다. */
    if (!parse_qualified_name(parser, &stmt.as.insert_stmt.table_name)) goto fail;

    /* 컬럼 목록이 있으면 "(...)" 부분을 읽는다. */
    if (match(parser, TOKEN_LPAREN)) {
        if (!parse_identifier_list(parser, &stmt.as.insert_stmt.columns, &stmt.as.insert_stmt.column_count)) goto fail;
        if (!consume(parser, TOKEN_RPAREN, "컬럼 목록 뒤에는 ')'가 필요합니다")) goto fail;
    }

    /* VALUES 키워드를 소비한다. */
    if (!consume(parser, TOKEN_VALUES, "VALUES가 필요합니다")) goto fail;
    /* VALUES 뒤 여는 괄호를 소비한다. */
    if (!consume(parser, TOKEN_LPAREN, "VALUES 뒤에는 '('가 필요합니다")) goto fail;
    /* 값 목록을 읽는다. */
    if (!parse_value_list(parser, &stmt.as.insert_stmt.values, &stmt.as.insert_stmt.value_count)) goto fail;
    /* 값 목록 뒤 닫는 괄호를 소비한다. */
    if (!consume(parser, TOKEN_RPAREN, "VALUES 목록 뒤에는 ')'가 필요합니다")) goto fail;

    /* 컬럼 목록이 있었으면 컬럼 개수와 값 개수가 같아야 한다. */
    if (stmt.as.insert_stmt.column_count > 0U &&
        stmt.as.insert_stmt.column_count != stmt.as.insert_stmt.value_count) {
        set_error(parser->err, "컬럼 개수와 값 개수는 같아야 합니다");
        goto fail;
    }

    /* 완성된 INSERT AST 를 호출자에게 넘긴다. */
    *out_statement = stmt;
    return true;
fail:
    /* 부분적으로 만든 INSERT AST 내부 메모리를 정리한다. */
    free_statement_contents(&stmt);
    return false;
}

/* SELECT ... FROM ... [WHERE ...] 문장 하나를 AST 로 만든다. */
static bool parse_select_statement(Parser *parser, Statement *out_statement) {
    /* SELECT AST 를 담을 임시 statement 구조체다. */
    Statement stmt = {0};
    /* 이 문장이 SELECT 라는 사실을 먼저 기록한다. */
    stmt.type = STATEMENT_SELECT;

    /* SELECT 키워드를 소비한다. */
    if (!consume(parser, TOKEN_SELECT, "SELECT가 필요합니다")) goto fail;

    /* '*' 이면 전체 컬럼 조회다. */
    if (match(parser, TOKEN_STAR)) {
        stmt.as.select_stmt.select_all = true;
    /* 아니면 명시된 컬럼 목록을 읽는다. */
    } else if (!parse_identifier_list(parser, &stmt.as.select_stmt.columns, &stmt.as.select_stmt.column_count)) {
        goto fail;
    }

    /* FROM 키워드를 소비한다. */
    if (!consume(parser, TOKEN_FROM, "SELECT 목록 뒤에는 FROM이 필요합니다")) goto fail;
    /* 대상 테이블 이름을 읽는다. */
    if (!parse_qualified_name(parser, &stmt.as.select_stmt.table_name)) goto fail;
    /* WHERE 절이 있으면 함께 읽는다. */
    if (!parse_where_clause(parser, &stmt.as.select_stmt.where)) goto fail;

    /* 완성된 SELECT AST 를 호출자에게 넘긴다. */
    *out_statement = stmt;
    return true;
fail:
    /* 부분적으로 만든 SELECT AST 내부 메모리를 정리한다. */
    free_statement_contents(&stmt);
    return false;
}

/* CREATE TABLE ... (...) 문장 하나를 AST 로 만든다. */
static bool parse_create_table_statement(Parser *parser, Statement *out_statement) {
    /* CREATE TABLE AST 를 담을 임시 statement 구조체다. */
    Statement stmt = {0};
    /* 이 문장이 CREATE TABLE 이라는 사실을 먼저 기록한다. */
    stmt.type = STATEMENT_CREATE_TABLE;

    /* CREATE 키워드를 소비한다. */
    if (!consume(parser, TOKEN_CREATE, "CREATE가 필요합니다")) goto fail;
    /* TABLE 키워드를 소비한다. */
    if (!consume(parser, TOKEN_TABLE, "CREATE 뒤에는 TABLE이 필요합니다")) goto fail;
    /* 생성할 테이블 이름을 읽는다. */
    if (!parse_qualified_name(parser, &stmt.as.create_table_stmt.table_name)) goto fail;
    /* 컬럼 정의 시작 괄호를 소비한다. */
    if (!consume(parser, TOKEN_LPAREN, "테이블 이름 뒤에는 '('가 필요합니다")) goto fail;
    /* 컬럼 정의 목록을 읽는다. */
    if (!parse_column_definition_list(parser,
                                      &stmt.as.create_table_stmt.columns,
                                      &stmt.as.create_table_stmt.column_types,
                                      &stmt.as.create_table_stmt.column_count)) goto fail;
    /* 컬럼 정의 끝 괄호를 소비한다. */
    if (!consume(parser, TOKEN_RPAREN, "컬럼 정의 뒤에는 ')'가 필요합니다")) goto fail;

    /* 완성된 CREATE TABLE AST 를 호출자에게 넘긴다. */
    *out_statement = stmt;
    return true;
fail:
    /* 부분적으로 만든 CREATE TABLE AST 내부 메모리를 정리한다. */
    free(stmt.as.create_table_stmt.table_name);
    free_string_array(stmt.as.create_table_stmt.columns, stmt.as.create_table_stmt.column_count);
    free_string_array(stmt.as.create_table_stmt.column_types, stmt.as.create_table_stmt.column_count);
    return false;
}

/* DROP TABLE ... 문장 하나를 AST 로 만든다. */
static bool parse_drop_table_statement(Parser *parser, Statement *out_statement) {
    /* DROP TABLE AST 를 담을 임시 statement 구조체다. */
    Statement stmt = {0};
    /* 이 문장이 DROP TABLE 이라는 사실을 먼저 기록한다. */
    stmt.type = STATEMENT_DROP_TABLE;

    /* DROP 키워드를 소비한다. */
    if (!consume(parser, TOKEN_DROP, "DROP이 필요합니다")) goto fail;
    /* TABLE 키워드를 소비한다. */
    if (!consume(parser, TOKEN_TABLE, "DROP 뒤에는 TABLE이 필요합니다")) goto fail;
    /* 삭제할 테이블 이름을 읽는다. */
    if (!parse_qualified_name(parser, &stmt.as.drop_table_stmt.table_name)) goto fail;

    /* 완성된 DROP TABLE AST 를 호출자에게 넘긴다. */
    *out_statement = stmt;
    return true;
fail:
    /* 부분적으로 만든 DROP TABLE AST 내부 메모리를 정리한다. */
    free_statement_contents(&stmt);
    return false;
}

/* DELETE FROM ... [WHERE ...] 문장 하나를 AST 로 만든다. */
static bool parse_delete_statement(Parser *parser, Statement *out_statement) {
    /* DELETE AST 를 담을 임시 statement 구조체다. */
    Statement stmt = {0};
    /* 이 문장이 DELETE 라는 사실을 먼저 기록한다. */
    stmt.type = STATEMENT_DELETE;

    /* DELETE 키워드를 소비한다. */
    if (!consume(parser, TOKEN_DELETE, "DELETE가 필요합니다")) goto fail;
    /* FROM 키워드를 소비한다. */
    if (!consume(parser, TOKEN_FROM, "DELETE 뒤에는 FROM이 필요합니다")) goto fail;
    /* 대상 테이블 이름을 읽는다. */
    if (!parse_qualified_name(parser, &stmt.as.delete_stmt.table_name)) goto fail;
    /* WHERE 절이 있으면 함께 읽는다. */
    if (!parse_where_clause(parser, &stmt.as.delete_stmt.where)) goto fail;

    /* 완성된 DELETE AST 를 호출자에게 넘긴다. */
    *out_statement = stmt;
    return true;
fail:
    /* 부분적으로 만든 DELETE AST 내부 메모리를 정리한다. */
    free_statement_contents(&stmt);
    return false;
}

static void initialize_parser(const TokenList *tokens, ErrorContext *err, Parser *parser,
                              StatementList *out_statements) {
    /* 파서가 읽을 토큰 배열을 연결한다. */
    parser->tokens  = tokens;
    /* 읽기 시작 위치를 0으로 맞춘다. */
    parser->current = 0U;
    /* 오류를 기록할 버퍼를 연결한다. */
    parser->err     = err;

    /* 출력 statement 배열도 빈 상태로 초기화한다. */
    out_statements->items    = NULL;
    out_statements->count    = 0U;
    out_statements->capacity = 0U;
}

static void skip_leading_semicolons(Parser *parser) {
    /* 비어 있는 문장을 허용하기 위해 연속된 세미콜론은 모두 건너뛴다. */
    while (match(parser, TOKEN_SEMICOLON)) {
    }
}

static StatementParserFn select_statement_parser(Parser *parser) {
    TokenType type = peek(parser)->type;

    /* 현재 토큰 종류에 등록된 문장 파서가 있으면 그것을 반환한다. */
    if ((size_t) type < (sizeof(STATEMENT_PARSERS) / sizeof(STATEMENT_PARSERS[0]))) {
        return STATEMENT_PARSERS[type];
    }

    /* 어떤 문장도 아니면 NULL 을 반환한다. */
    return NULL;
}

static bool parse_next_statement(Parser *parser, Statement *statement, StatementList *out_statements) {
    /* 현재 첫 토큰에 맞는 문장 파서를 선택한다. */
    StatementParserFn parser_fn = select_statement_parser(parser);

    /* 지원하지 않는 시작 토큰이면 문장 종류 자체가 잘못된 것이다. */
    if (parser_fn == NULL) {
        parse_error(parser, "INSERT, SELECT, CREATE TABLE, DROP TABLE 또는 DELETE가 필요합니다");
        free_statement_list(out_statements);
        return false;
    }

    /* 선택한 전용 파서로 문장 하나를 AST 로 만든다. */
    if (!parser_fn(parser, statement)) {
        free_statement_list(out_statements);
        return false;
    }

    /* 완성된 문장을 결과 StatementList 뒤에 붙인다. */
    if (!append_statement(out_statements, *statement, parser->err)) {
        free_statement_contents(statement);
        free_statement_list(out_statements);
        return false;
    }

    /* 문장 하나 파싱 성공을 반환한다. */
    return true;
}

static bool consume_statement_terminator(Parser *parser, StatementList *out_statements) {
    /* 세미콜론이 있으면 문장 끝으로 인정한다. */
    if (match(parser, TOKEN_SEMICOLON)) {
        /* 연속된 세미콜론도 모두 소비해 빈 문장을 허용한다. */
        while (match(parser, TOKEN_SEMICOLON)) {
        }
        return true;
    }

    /* 입력 끝이면 마지막 문장의 세미콜론을 생략해도 허용한다. */
    if (is_at_end(parser)) {
        return true;
    }

    /* 여기까지 왔으면 문장 구분자가 없어 파싱을 계속할 수 없다. */
    free_statement_list(out_statements);
    return parse_error(parser, "문장 끝에는 ';'가 필요합니다");
}

/*
 * TokenList 전체를 처음부터 끝까지 읽어 StatementList 로 바꾼다.
 *
 * 이 함수의 핵심 흐름은 다음과 같다.
 * 1. 세미콜론을 건너뛴다.
 * 2. 현재 토큰의 종류를 보고 어떤 parse_* 함수를 부를지 고른다.
 * 3. 완성된 statement 를 StatementList 에 넣는다.
 * 4. 문장 끝의 세미콜론을 소비한다.
 */
bool parse_tokens(const TokenList *tokens, StatementList *out_statements, ErrorContext *err) {
    /* 토큰 배열 위를 이동하며 문장을 만드는 파서 상태다. */
    Parser parser;

    /* 파서 상태와 출력 배열을 초기화한다. */
    initialize_parser(tokens, err, &parser, out_statements);

    /* EOF 토큰을 만날 때까지 문장 파싱을 반복한다. */
    while (!is_at_end(&parser)) {
        /* 문장 하나를 담을 임시 AST 구조체다. */
        Statement statement;

        /* union 내부 포인터들을 안전하게 0으로 초기화한다. */
        memset(&statement, 0, sizeof(statement));

        /* 빈 문장(세미콜론만 여러 개)도 허용하므로 앞쪽 세미콜론은 모두 건너뛴다. */
        skip_leading_semicolons(&parser);

        /* 세미콜론만 남아 있다가 EOF 를 만나면 바로 종료한다. */
        if (is_at_end(&parser)) {
            break;
        }

        /* 현재 첫 토큰을 보고 어떤 SQL 문장인지 분기한다. */
        if (!parse_next_statement(&parser, &statement, out_statements)) {
            return false;
        }

        /* 한 문장이 끝났으면 세미콜론을 소비하고 다음 문장으로 넘어간다. */
        if (!consume_statement_terminator(&parser, out_statements)) {
            return false;
        }
    }

    return true;
}

/* StatementList 전체에 대해 내부 문자열과 배열까지 모두 정리한다. */
void free_statement_list(StatementList *statements) {
    /* statement 배열을 순회할 인덱스다. */
    size_t i;

    /* NULL 이면 정리할 대상이 없다. */
    if (statements == NULL) {
        return;
    }

    /* 각 statement 가 내부적으로 가진 문자열/배열을 하나씩 정리한다. */
    for (i = 0; i < statements->count; ++i) {
        free_statement_contents(&statements->items[i]);
    }

    /* statement 구조체 배열 자체를 해제한다. */
    free(statements->items);
    /* 정리 후 포인터를 비운다. */
    statements->items    = NULL;
    /* 문장 개수를 0으로 되돌린다. */
    statements->count    = 0U;
    /* 용량도 0으로 되돌린다. */
    statements->capacity = 0U;
}
