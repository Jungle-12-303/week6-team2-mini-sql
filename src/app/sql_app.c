#include <stdlib.h>

#include "executor/sql_executor.h"
#include "frontend/sql_frontend.h"
#include "mini_sql.h"
#include "session/sql_session.h"
#include "storage/storage_engine.h"

/*
 * SqlApp 은 애플리케이션의 조립과 생명주기만 담당한다.
 * 실제 실행 흐름은 SqlSession 이 맡고,
 * app 은 그 세션이 사용할 frontend/executor/storage 를 묶어 놓는다.
 */
struct SqlApp {
    StorageEngine    storage_engine;
    SqlFrontend      frontend;
    SqlExecutor      executor;
    SqlSession       session;
    ExecutionContext execution_context;
};

static bool validate_app_config(const SqlAppConfig *config, ErrorContext *err) {
    /* 앱 설정 객체와 DB 경로는 앱 조립의 최소 입력이다. */
    if (config == NULL || config->db_path == NULL) {
        /* 둘 중 하나라도 없으면 앱을 만들 수 없다. */
        set_error(err, "애플리케이션 설정이 올바르지 않습니다");
        return false;
    }

    /* 앱 조립 가능한 설정이므로 성공을 반환한다. */
    return true;
}

static SqlApp *allocate_sql_app(ErrorContext *err) {
    /* 앱 전체를 0으로 초기화된 상태로 한 번에 할당한다. */
    SqlApp *app = calloc(1U, sizeof(*app));

    /* 할당에 실패하면 더 진행할 수 없으므로 오류를 남긴다. */
    if (app == NULL) {
        set_error(err, "애플리케이션을 생성하는 중 메모리가 부족합니다");
        return NULL;
    }

    /* 할당된 앱 포인터를 반환한다. */
    return app;
}

static void initialize_execution_context(SqlApp *app, const SqlAppConfig *config) {
    /* 실행 문맥이 참조할 저장 엔진을 앱 내부 엔진으로 연결한다. */
    app->execution_context.storage_engine = &app->storage_engine;
    /* 출력 스트림은 설정값이 있으면 그것을, 없으면 stdout 을 사용한다. */
    app->execution_context.output = config->output != NULL ? config->output : stdout;
    /* 결과 포맷터도 설정값을 그대로 연결한다. */
    app->execution_context.formatter = config->formatter;
}

static void initialize_runtime_components(SqlApp *app) {
    /* 프런트엔드를 초기화한다. */
    sql_frontend_init(&app->frontend);
    /* 실행기를 초기화한다. */
    sql_executor_init(&app->executor);
    /* 세션에 프런트엔드, 실행기, 실행 문맥을 연결한다. */
    sql_session_init(&app->session, &app->frontend, &app->executor, &app->execution_context);
}

SqlApp *sql_app_create(const SqlAppConfig *config, ErrorContext *err) {
    /* 조립할 앱 객체를 담을 포인터다. */
    SqlApp *app;

    /* 1. 앱 설정이 올바른지 먼저 확인한다. */
    if (!validate_app_config(config, err)) {
        return NULL;
    }

    /* 2. 앱 구조체 메모리를 할당한다. */
    app = allocate_sql_app(err);
    /* 메모리 할당에 실패했으면 더 진행하지 않는다. */
    if (app == NULL) {
        return NULL;
    }

    /* 3. 저장 엔진 구현체를 현재 설정에 맞게 만든다. */
    if (!storage_engine_create(&app->storage_engine, config->storage_kind, config->db_path, err)) {
        /* 저장 엔진 생성에 실패하면 앱 구조체만 정리한다. */
        free(app);
        return NULL;
    }

    /* 4. 실행 문맥에 저장 엔진, 출력, 포맷터를 연결한다. */
    initialize_execution_context(app, config);
    /* 5. 프런트엔드, 실행기, 세션을 초기화한다. */
    initialize_runtime_components(app);
    /* 6. 조립이 끝난 앱을 반환한다. */
    return app;
}

void sql_app_destroy(SqlApp *app) {
    /* NULL 이면 정리할 대상이 없다. */
    if (app == NULL) {
        return;
    }

    /* 저장 엔진이 가진 파일/메모리 자원을 먼저 정리한다. */
    storage_engine_destroy(&app->storage_engine);
    /* 마지막으로 앱 구조체 메모리를 해제한다. */
    free(app);
}

SqlSession *sql_app_session(SqlApp *app) {
    /* 앱 포인터가 없으면 세션도 꺼낼 수 없다. */
    if (app == NULL) {
        return NULL;
    }

    /* 앱 내부 세션 포인터를 외부에 돌려준다. */
    return &app->session;
}
