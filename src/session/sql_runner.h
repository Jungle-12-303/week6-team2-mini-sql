#ifndef SQL_RUNNER_H
#define SQL_RUNNER_H

#include "mini_sql.h"
#include "session/sql_session.h"

/*
 * SqlRunRequest 는 argv 를 해석해 얻은 "실행 요청"이다.
 * main 은 argc/argv 를 직접 판단하지 않고,
 * 이 요청 객체만 보고 앱 설정과 실행기를 준비한다.
 */
typedef enum SqlRunMode {
    SQL_RUN_MODE_CLI = 0,
    SQL_RUN_MODE_FILES = 1
} SqlRunMode;

typedef struct SqlRunRequest {
    bool show_help;
    SqlRunMode mode;
    SqlAppConfig app_config;
    const char **file_paths;
    size_t file_count;
    size_t file_capacity;
} SqlRunRequest;

typedef struct SqlRunner SqlRunner;

/*
 * argv 를 읽어 SqlRunRequest 로 정규화한다.
 * 이 함수 이후에는 "파일이 있나?" 같은 암묵 조건 대신 mode 만 보면 된다.
 */
bool parse_run_request(int argc, char **argv, SqlRunRequest *out_request, ErrorContext *err);

/* SqlRunRequest 가 내부적으로 잡은 메모리를 정리한다. */
void cleanup_run_request(SqlRunRequest *request);

/* 사용자에게 보여줄 실행 도움말을 출력한다. */
void print_run_request_usage(const char *program_name);

/*
 * SqlRunner 는 "어떤 입력 모드를 어떤 방식으로 실행할지"를 추상화한 인터페이스다.
 *
 * 구현체:
 *   CliRunner  - 인터랙티브 CLI 실행
 *   FileRunner - .sql 파일 목록 실행
 */
SqlRunner *sql_runner_create(const SqlRunRequest *request, SqlSession *session, ErrorContext *err);
bool sql_runner_run(SqlRunner *runner, ErrorContext *err);
void sql_runner_destroy(SqlRunner *runner);

#endif
