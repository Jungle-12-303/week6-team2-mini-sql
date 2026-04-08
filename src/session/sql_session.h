#ifndef SQL_SESSION_H
#define SQL_SESSION_H

#include "frontend/sql_frontend.h"
#include "executor/sql_executor.h"
#include "mini_sql.h"

/*
 * SqlSession 은 "입력 하나의 실행 흐름"을 담당한다.
 *
 *   입력 텍스트
 *     -> frontend.compile()
 *     -> executor.execute()
 *     -> 결과 출력 / 저장 반영
 *
 * CLI, SQL 파일, 향후 socket/update loop 는 모두 이 세션 인터페이스를 재사용한다.
 */
struct SqlSession {
    const SqlFrontend *frontend;
    const SqlExecutor *executor;
    const ExecutionContext *execution_context;
};

void sql_session_init(SqlSession *session, const SqlFrontend *frontend,
                      const SqlExecutor *executor, const ExecutionContext *execution_context);

bool sql_session_execute(SqlSession *session, const SqlInput *input, ErrorContext *err);
bool sql_session_execute_file(SqlSession *session, const char *path, ErrorContext *err);

#endif
