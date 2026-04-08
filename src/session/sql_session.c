#include "session/sql_session.h"

#include <stdlib.h>

/*
 * SqlSession 은 실제 요청 실행 순서를 드러내는 계층이다.
 *
 *   입력 텍스트
 *     -> sql_frontend_compile()
 *     -> sql_executor_execute()
 *     -> 결과 출력 / 파일 반영
 */

static const char *sql_input_kind_label(SqlInputKind kind) {
    /* 입력 출처 enum 값을 사람이 읽을 수 있는 문자열로 바꾼다. */
    switch (kind) {
    /* CLI 에서 직접 입력한 경우다. */
    case SQL_INPUT_CLI:
        return "대화형 CLI";
    /* .sql 파일에서 읽은 경우다. */
    case SQL_INPUT_FILE:
        return "파일";
    /* 향후 소켓 입력을 붙일 때 사용할 자리다. */
    case SQL_INPUT_SOCKET:
        return "소켓";
    /* 향후 업데이트 루프 입력을 붙일 때 사용할 자리다. */
    case SQL_INPUT_UPDATE_LOOP:
        return "업데이트 루프";
    }

    /* 정의되지 않은 입력 종류면 일반적인 이름으로 되돌린다. */
    return "입력";
}

void sql_session_init(SqlSession *session, const SqlFrontend *frontend,
                      const SqlExecutor *executor, const ExecutionContext *execution_context) {
    /* 세션이 사용할 프런트엔드를 저장한다. */
    session->frontend = frontend;
    /* 세션이 사용할 실행기를 저장한다. */
    session->executor = executor;
    /* 세션이 실행 시 사용할 저장 엔진과 출력 문맥을 저장한다. */
    session->execution_context = execution_context;
}

static bool validate_sql_input(SqlSession *session, const SqlInput *input, ErrorContext *err) {
    /* 세션, 프런트엔드, 실행기, 실행 문맥, 입력 텍스트가 모두 있어야 한다. */
    if (session == NULL || session->frontend == NULL || session->executor == NULL ||
        session->execution_context == NULL || input == NULL || input->text == NULL) {
        /* 하나라도 비어 있으면 실행할 수 없으므로 오류를 남긴다. */
        set_error(err, "SQL 입력 정보가 올바르지 않습니다");
        /* 검증 실패를 반환한다. */
        return false;
    }

    /* 실행 가능한 입력이므로 성공을 반환한다. */
    return true;
}

static bool compile_sql_input(SqlSession *session, const SqlInput *input,
                              StatementList *statements, ErrorContext *err) {
    /* 세션이 가진 프런트엔드에 원문 SQL 을 넘겨 AST 목록으로 컴파일한다. */
    return sql_frontend_compile(session->frontend, input->text, statements, err);
}

static bool execute_compiled_statements(SqlSession *session, const StatementList *statements,
                                        ErrorContext *err) {
    /* 프런트엔드가 만든 AST 목록을 실행기에 넘겨 실제 동작을 수행한다. */
    return sql_executor_execute(session->executor, statements, session->execution_context, err);
}

static void set_session_error(const SqlInput *input, const ErrorContext *nested_err,
                              ErrorContext *err) {
    /* 입력 출처 이름이 있으면 오류 메시지에 함께 넣는다. */
    if (input->source_name != NULL && input->source_name[0] != '\0') {
        /* 예: 파일 입력(example.sql) 처리 실패: ... */
        set_error(err, "%s 입력(%s) 처리 실패: %s",
                  sql_input_kind_label(input->kind), input->source_name, nested_err->buf);
    } else {
        /* 출처 이름이 없으면 종류만 넣어 더 간단하게 남긴다. */
        set_error(err, "%s 입력 처리 실패: %s",
                  sql_input_kind_label(input->kind), nested_err->buf);
    }
}

bool sql_session_execute(SqlSession *session, const SqlInput *input, ErrorContext *err) {
    /* 프런트엔드가 만들어 낼 AST 목록을 받을 버퍼다. */
    StatementList statements = {0};
    /* 하위 계층 오류를 먼저 받은 뒤 세션 문맥을 덧붙이기 위한 버퍼다. */
    ErrorContext nested_err = {0};
    /* 성공 여부를 마지막에 한 번에 판단하기 위한 플래그다. */
    bool ok = false;

    /* 먼저 세션과 입력이 실행 가능한 상태인지 검증한다. */
    if (!validate_sql_input(session, input, err)) {
        return false;
    }

    /* 1. 입력 SQL 문자열을 AST 목록으로 컴파일한다. */
    if (!compile_sql_input(session, input, &statements, &nested_err)) {
        goto fail;
    }

    /* 2. 컴파일된 AST 목록을 실행기에 넘겨 실제 작업을 수행한다. */
    if (!execute_compiled_statements(session, &statements, &nested_err)) {
        goto fail;
    }

    /* 여기까지 왔으면 컴파일과 실행이 모두 성공했다. */
    ok = true;

fail:
    /* 성공/실패와 상관없이 AST 목록이 잡은 메모리를 정리한다. */
    free_statement_list(&statements);

    /* 성공이면 바로 true 를 반환한다. */
    if (ok) {
        return true;
    }

    /* 실패면 하위 오류에 입력 출처 문맥을 덧붙여 최종 오류를 만든다. */
    set_session_error(input, &nested_err, err);
    /* 세션 실행 실패를 반환한다. */
    return false;
}

static bool load_sql_file_text(const char *path, char **out_sql_text, ErrorContext *err) {
    /* 파일 경로 자체가 비어 있으면 읽기를 시작할 수 없다. */
    if (path == NULL) {
        set_error(err, "SQL 파일 경로가 올바르지 않습니다");
        return false;
    }

    /* 파일 전체를 한 번에 읽어 문자열로 돌려준다. */
    return read_file_all(path, out_sql_text, err);
}

static SqlInput build_file_input(const char *path, const char *sql_text) {
    /* 파일 입력을 표현할 SqlInput 구조체를 만든다. */
    SqlInput input;

    /* 입력 종류를 파일로 지정한다. */
    input.kind = SQL_INPUT_FILE;
    /* 출처 이름은 파일 경로다. */
    input.source_name = path;
    /* 실제 실행할 텍스트는 읽어 온 SQL 원문이다. */
    input.text = sql_text;
    /* 완성된 입력 구조체를 반환한다. */
    return input;
}

bool sql_session_execute_file(SqlSession *session, const char *path, ErrorContext *err) {
    /* 파일에서 읽어 온 SQL 문자열을 담을 포인터다. */
    char *sql_text = NULL;
    /* 파일 입력을 세션 공통 경로로 넘기기 위한 구조체다. */
    SqlInput input;
    /* 최종 실행 성공 여부를 담는다. */
    bool ok;

    /* 1. SQL 파일 전체를 메모리로 읽어 온다. */
    if (!load_sql_file_text(path, &sql_text, err)) {
        return false;
    }

    /* 2. 읽어 온 문자열을 파일 입력 구조체로 묶는다. */
    input = build_file_input(path, sql_text);
    /* 3. 공통 세션 실행 경로로 넘긴다. */
    ok = sql_session_execute(session, &input, err);
    /* 4. 파일 문자열 메모리를 정리한다. */
    free(sql_text);
    /* 5. 최종 성공 여부를 반환한다. */
    return ok;
}
