#include "frontend/sql_frontend.h"

/*
 * SqlFrontend 는 실행을 하지 않는다.
 * 텍스트를 토큰화하고, 토큰을 StatementList 로 바꾸는 데까지만 책임진다.
 */

void sql_frontend_init(SqlFrontend *frontend) {
    /* 현재 프런트엔드는 별도 내부 상태가 없으므로 예약 바이트만 초기화한다. */
    frontend->reserved = 0U;
}

static bool validate_compile_request(const SqlFrontend *frontend, const char *sql,
                                     StatementList *out_statements, ErrorContext *err) {
    /* 프런트엔드 포인터, 원문 SQL, 결과 버퍼가 모두 있어야 컴파일할 수 있다. */
    if (frontend == NULL || sql == NULL || out_statements == NULL) {
        /* 하나라도 비어 있으면 컴파일 요청 자체가 잘못된 것이다. */
        set_error(err, "SQL 프런트엔드 입력이 올바르지 않습니다");
        return false;
    }

    /* 컴파일 가능한 요청이므로 성공을 반환한다. */
    return true;
}

bool sql_frontend_compile(const SqlFrontend *frontend, const char *sql,
                          StatementList *out_statements, ErrorContext *err) {
    /* 토큰화 결과를 담을 임시 토큰 배열이다. */
    TokenList tokens = {0};

    /* 1. 컴파일 요청이 올바른지 먼저 확인한다. */
    if (!validate_compile_request(frontend, sql, out_statements, err)) {
        return false;
    }

    /* 2. 원문 SQL 을 토큰 배열로 바꾼다. */
    if (!tokenize_sql(sql, &tokens, err)) {
        return false;
    }

    /* 3. 토큰 배열을 AST 목록으로 바꾼다. */
    if (!parse_tokens(&tokens, out_statements, err)) {
        /* 파싱에 실패해도 토큰 메모리는 반드시 정리한다. */
        free_token_list(&tokens);
        return false;
    }

    /* 4. 파싱이 끝났으니 토큰 메모리를 정리한다. */
    free_token_list(&tokens);
    /* 5. 최종 컴파일 성공을 반환한다. */
    return true;
}
