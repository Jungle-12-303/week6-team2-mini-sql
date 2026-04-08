#include <stdio.h>
#include <stdlib.h>

#include "mini_sql.h"
#include "session/sql_runner.h"

/*
 * main 은 프로그램의 시작점이다.
 * 여기서는 실제 SQL 로직을 직접 처리하지 않고,
 * "실행 명령을 해석하고, 앱을 조립하고, 실행기를 돌리는 일"만 담당한다.
 *
 * 큰 흐름:
 *   argv
 *     -> SqlRunRequest
 *     -> SqlApp
 *     -> SqlRunner
 *     -> run()
 */

static void print_error(const ErrorContext *err) {
    /* 에러 버퍼에 담긴 내용을 사용자에게 출력한다. */
    fprintf(stderr, "오류: %s\n", err->buf);
}

static const char *program_name_for_usage(char **argv) {
    const char *wrapped_name = getenv("MSQL_PROGRAM_NAME");

    if (wrapped_name != NULL && wrapped_name[0] != '\0') {
        return wrapped_name;
    }

    return argv[0];
}

int main(int argc, char **argv) {
    /* argv 를 해석한 최종 실행 요청을 담는다. */
    SqlRunRequest request;
    /* 실행 요청으로부터 조립한 앱 객체다. */
    SqlApp *app = NULL;
    /* 실행 요청 모드에 맞는 구체 실행기다. */
    SqlRunner *runner = NULL;
    /* 모든 단계가 공통으로 쓰는 에러 버퍼다. */
    ErrorContext err = {0};
    /* 마지막 종료 코드를 담는다. */
    int exit_code = 1;
    /* 사용법 출력 시 보여줄 실행 프로그램 이름이다. */
    const char *program_name = program_name_for_usage(argv);

    /* 1. 프로그램 인자를 읽어 실행 요청 객체로 정규화한다. */
    if (!parse_run_request(argc, argv, &request, &err)) {
        /* 실행 요청 파싱이 실패했으면 오류를 먼저 보여준다. */
        print_error(&err);
        /* 사용자가 바로 수정할 수 있도록 사용법도 함께 보여준다. */
        print_run_request_usage(program_name);
        /* 요청 객체가 확보한 메모리를 정리한다. */
        cleanup_run_request(&request);
        /* 비정상 종료 코드를 반환한다. */
        return 1;
    }

    /* 2. help 요청이면 실제 실행 없이 사용법만 보여주고 끝낸다. */
    if (request.show_help) {
        /* 도움말만 출력한다. */
        print_run_request_usage(program_name);
        /* 요청 객체가 확보한 메모리를 정리한다. */
        cleanup_run_request(&request);
        /* 정상 종료 코드를 반환한다. */
        return 0;
    }

    /* 3. 실행 요청 안의 앱 설정으로 앱을 조립한다. */
    app = sql_app_create(&request.app_config, &err);
    /* 앱 생성에 실패하면 더 진행할 수 없다. */
    if (app == NULL) {
        /* 앱 생성 오류를 보여준다. */
        print_error(&err);
        /* 실행 요청이 가진 메모리를 정리한다. */
        cleanup_run_request(&request);
        /* 비정상 종료 코드를 반환한다. */
        return 1;
    }

    /* 4. 앱 안에 들어 있는 세션과 실행 요청을 이용해 실행기를 만든다. */
    runner = sql_runner_create(&request, sql_app_session(app), &err);
    /* 실행기 생성에 실패하면 앱만 정리하고 끝낸다. */
    if (runner == NULL) {
        /* 실행기 생성 오류를 보여준다. */
        print_error(&err);
        /* 이미 만든 앱을 정리한다. */
        sql_app_destroy(app);
        /* 실행 요청 메모리를 정리한다. */
        cleanup_run_request(&request);
        /* 비정상 종료 코드를 반환한다. */
        return 1;
    }

    /* 5. 구체 실행기가 무엇이든 공통 run 인터페이스로 실행한다. */
    if (!sql_runner_run(runner, &err)) {
        /* 실행 중 오류를 보여준다. */
        print_error(&err);
        /* 실패 종료 코드를 기록한다. */
        exit_code = 1;
    } else {
        /* 성공 종료 코드를 기록한다. */
        exit_code = 0;
    }

    /* 6. 실행기가 잡고 있던 내부 상태를 정리한다. */
    sql_runner_destroy(runner);
    /* 7. 앱이 소유한 저장 엔진과 세션을 정리한다. */
    sql_app_destroy(app);
    /* 8. 실행 요청이 잡고 있던 파일 목록 메모리를 정리한다. */
    cleanup_run_request(&request);
    /* 9. 최종 종료 코드를 운영체제에 돌려준다. */
    return exit_code;
}
