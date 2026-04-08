#include "mini_sql.h"
#include "repl.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const char *program_name) {
    printf("Usage: %s [--db <directory>] [sql-file]\n", program_name);
    printf("Example (file): %s --db ./db ./examples/demo.sql\n", program_name);
    printf("Example (interactive): %s --db ./db\n", program_name);
    printf("Shortcut: make cli\n");
}

int main(int argc, char **argv) {
    const char *db_path = "db";
    const char *sql_path = NULL;
    char error_buf[MSQL_ERROR_SIZE];
    MiniSqlAppConfig config;
    MiniSqlApp *app;
    bool ok;
    int i;

    /* 1. CLI 인자를 읽어서 DB 경로와 SQL 파일 경로를 결정한다. */
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        if (strcmp(argv[i], "--db") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing directory after --db\n");
                return 1;
            }
            db_path = argv[++i];
            continue;
        }

        if (sql_path == NULL) {
            sql_path = argv[i];
            continue;
        }

        fprintf(stderr, "Error: unexpected argument '%s'\n", argv[i]);
        print_usage(argv[0]);
        return 1;
    }

    /* 2. 앱 객체를 만들고, 내부에서 SQL 처리기와 저장 엔진을 조립한다. */
    config.db_path = db_path;
    config.output = stdout;
    app = mini_sql_app_create(&config, error_buf, sizeof(error_buf));
    if (app == NULL) {
        fprintf(stderr, "Error: %s\n", error_buf);
        return 1;
    }

    /* 3. SQL 파일이 없으면 REPL 모드로 들어가 사용자의 입력을 계속 처리한다. */
    if (sql_path == NULL) {
        ok = run_repl(app, error_buf, sizeof(error_buf));
    } else {
        /* 4. SQL 파일이 있으면 앱이 파일 읽기와 SQL 실행을 한 번에 처리한다. */
        ok = mini_sql_app_run_file(app, sql_path, error_buf, sizeof(error_buf));
    }

    if (!ok) {
        fprintf(stderr, "Error: %s\n", error_buf);
        mini_sql_app_destroy(app);
        return 1;
    }

    /* 5. 앱이 소유한 저장 엔진과 처리기를 정리하고 종료한다. */
    mini_sql_app_destroy(app);
    return 0;
}
