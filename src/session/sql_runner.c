#include "session/sql_runner.h"
#include "session/sql_cli.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct SqlRunnerOps SqlRunnerOps;

struct SqlRunner {
    const SqlRunnerOps *ops;
    SqlSession *session;
    const SqlRunRequest *request;
};

struct SqlRunnerOps {
    bool (*run)(SqlRunner *runner, ErrorContext *err);
};

typedef struct SqlRunnerSpec {
    SqlRunMode mode;
    const SqlRunnerOps *ops;
} SqlRunnerSpec;

static void initialize_run_request(SqlRunRequest *request) {
    request->show_help = false;
    request->mode = SQL_RUN_MODE_CLI;
    request->app_config.storage_kind = STORAGE_ENGINE_CSV;
    request->app_config.db_path = "db";
    request->app_config.output = stdout;
    request->app_config.formatter = NULL;
    request->file_paths = NULL;
    request->file_count = 0U;
    request->file_capacity = 0U;
}

void print_run_request_usage(const char *program_name) {
    printf("사용법: %s [--db <디렉터리>] [sql-파일 ...]\n", program_name);
    printf("예시(파일 실행): %s --db ./db ./examples/step1.sql ./examples/step2.sql\n",
           program_name);
    printf("예시(CLI 실행): %s --db ./db\n", program_name);
    printf(".sql 파일이 하나 이상 들어오면 순서대로 모두 실행합니다.\n");
    printf(".sql 파일이 없으면 대화형 SQL CLI를 시작합니다.\n");
    printf("바로 실행: make cli\n");
}

static bool has_sql_extension(const char *path) {
    size_t length = strlen(path);

    if (length < 4U) {
        return false;
    }

    return strings_equal_ci(path + length - 4U, ".sql");
}

static bool ensure_file_path_capacity(SqlRunRequest *request, ErrorContext *err) {
    const char **new_items;
    size_t new_capacity;

    if (request->file_count < request->file_capacity) {
        return true;
    }

    new_capacity = request->file_capacity == 0U ? 4U : request->file_capacity * 2U;
    new_items = realloc((void *) request->file_paths, new_capacity * sizeof(*new_items));
    if (new_items == NULL) {
        set_error(err, "실행 명령을 만드는 중 메모리가 부족합니다");
        return false;
    }

    request->file_paths = new_items;
    request->file_capacity = new_capacity;
    return true;
}

static bool append_sql_file_path(SqlRunRequest *request, const char *path, ErrorContext *err) {
    if (!ensure_file_path_capacity(request, err)) {
        return false;
    }

    request->file_paths[request->file_count] = path;
    request->file_count += 1U;
    return true;
}

static bool validate_db_path(const char *path, ErrorContext *err) {
    struct stat st;

    if (stat(path, &st) != 0) {
        set_error(err, "데이터베이스 디렉터리가 존재하지 않습니다: %s", path);
        return false;
    }

    if (!S_ISDIR(st.st_mode)) {
        set_error(err, "데이터베이스 경로가 디렉터리가 아닙니다: %s", path);
        return false;
    }

    return true;
}

static bool consume_db_option(int argc, char **argv, int *index, SqlRunRequest *request,
                              ErrorContext *err) {
    if (*index + 1 >= argc) {
        set_error(err, "--db 뒤에 디렉터리 경로가 필요합니다");
        return false;
    }

    request->app_config.db_path = argv[++(*index)];
    return true;
}

bool parse_run_request(int argc, char **argv, SqlRunRequest *out_request, ErrorContext *err) {
    int i;
    bool db_path_explicit = false;

    if (out_request == NULL) {
        set_error(err, "실행 명령 버퍼가 올바르지 않습니다");
        return false;
    }

    initialize_run_request(out_request);

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            out_request->show_help = true;
            return true;
        }

        if (strcmp(arg, "--db") == 0) {
            if (!consume_db_option(argc, argv, &i, out_request, err)) {
                return false;
            }
            db_path_explicit = true;
            continue;
        }

        if (!has_sql_extension(arg)) {
            set_error(err, "알 수 없는 인자입니다: %s", arg);
            return false;
        }

        if (!append_sql_file_path(out_request, arg, err)) {
            return false;
        }
    }

    if (db_path_explicit && !validate_db_path(out_request->app_config.db_path, err)) {
        return false;
    }

    out_request->mode = out_request->file_count > 0U ? SQL_RUN_MODE_FILES : SQL_RUN_MODE_CLI;
    return true;
}

void cleanup_run_request(SqlRunRequest *request) {
    if (request == NULL) {
        return;
    }

    free((void *) request->file_paths);
    request->file_paths = NULL;
    request->file_count = 0U;
    request->file_capacity = 0U;
}

static bool file_runner_run(SqlRunner *runner, ErrorContext *err) {
    size_t i;

    for (i = 0; i < runner->request->file_count; ++i) {
        ErrorContext nested_err = {0};

        if (!sql_session_execute_file(runner->session, runner->request->file_paths[i], &nested_err)) {
            set_error(err, "%s 실행 중 오류가 발생했습니다: %s",
                      runner->request->file_paths[i], nested_err.buf);
            return false;
        }
    }

    return true;
}

static bool cli_runner_run(SqlRunner *runner, ErrorContext *err) {
    return run_sql_cli(runner->session, err);
}

static const SqlRunnerOps FILE_RUNNER_OPS = {
    file_runner_run
};

static const SqlRunnerOps CLI_RUNNER_OPS = {
    cli_runner_run
};

static const SqlRunnerSpec RUNNER_SPECS[] = {
    {SQL_RUN_MODE_CLI, &CLI_RUNNER_OPS},
    {SQL_RUN_MODE_FILES, &FILE_RUNNER_OPS}
};

static const SqlRunnerOps *find_runner_ops(SqlRunMode mode) {
    size_t i;

    for (i = 0; i < sizeof(RUNNER_SPECS) / sizeof(RUNNER_SPECS[0]); ++i) {
        if (RUNNER_SPECS[i].mode == mode) {
            return RUNNER_SPECS[i].ops;
        }
    }

    return NULL;
}

SqlRunner *sql_runner_create(const SqlRunRequest *request, SqlSession *session, ErrorContext *err) {
    const SqlRunnerOps *ops;
    SqlRunner *runner;

    if (request == NULL || session == NULL) {
        set_error(err, "실행기 생성 요청이 올바르지 않습니다");
        return NULL;
    }

    ops = find_runner_ops(request->mode);
    if (ops == NULL) {
        set_error(err, "실행 모드에 맞는 실행기를 찾지 못했습니다");
        return NULL;
    }

    runner = malloc(sizeof(*runner));
    if (runner == NULL) {
        set_error(err, "실행기를 만드는 중 메모리가 부족합니다");
        return NULL;
    }

    runner->ops = ops;
    runner->session = session;
    runner->request = request;
    return runner;
}

bool sql_runner_run(SqlRunner *runner, ErrorContext *err) {
    if (runner == NULL || runner->ops == NULL || runner->ops->run == NULL || runner->request == NULL) {
        set_error(err, "실행기가 올바르지 않습니다");
        return false;
    }

    return runner->ops->run(runner, err);
}

void sql_runner_destroy(SqlRunner *runner) {
    free(runner);
}
