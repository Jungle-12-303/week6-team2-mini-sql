#include "mini_sql.h"
#include "sql_processor.h"
#include "statement_executor.h"
#include "storage_engine.h"

#include <stdlib.h>

struct MiniSqlApp {
    StorageEngine storage_engine;
    StatementExecutor statement_executor;
    SqlProcessor sql_processor;
    ExecutionContext execution_context;
};

bool init_file_execution_context(ExecutionContext *context, const char *db_path, FILE *output,
                                 char *error_buf, size_t error_size) {
    StorageEngine *storage_engine;

    if (context == NULL || db_path == NULL) {
        set_error(error_buf, error_size, "invalid execution context configuration");
        return false;
    }

    storage_engine = calloc(1U, sizeof(*storage_engine));
    if (storage_engine == NULL) {
        set_error(error_buf, error_size, "out of memory while creating execution context");
        return false;
    }

    if (!storage_engine_create_file(storage_engine, db_path, error_buf, error_size)) {
        free(storage_engine);
        return false;
    }

    context->storage_engine = storage_engine;
    context->output = output == NULL ? stdout : output;
    return true;
}

void destroy_execution_context(ExecutionContext *context) {
    if (context == NULL) {
        return;
    }

    if (context->storage_engine != NULL) {
        storage_engine_destroy(context->storage_engine);
        free(context->storage_engine);
    }

    context->storage_engine = NULL;
    context->output = NULL;
}

MiniSqlApp *mini_sql_app_create(const MiniSqlAppConfig *config, char *error_buf, size_t error_size) {
    MiniSqlApp *app;

    if (config == NULL || config->db_path == NULL) {
        set_error(error_buf, error_size, "invalid application configuration");
        return NULL;
    }

    app = calloc(1U, sizeof(*app));
    if (app == NULL) {
        set_error(error_buf, error_size, "out of memory while creating application");
        return NULL;
    }

    if (!init_file_execution_context(&app->execution_context, config->db_path, config->output, error_buf, error_size)) {
        free(app);
        return NULL;
    }

    app->storage_engine = *app->execution_context.storage_engine;
    statement_executor_init(&app->statement_executor);
    sql_processor_init(&app->sql_processor, &app->statement_executor);
    return app;
}

void mini_sql_app_destroy(MiniSqlApp *app) {
    if (app == NULL) {
        return;
    }

    destroy_execution_context(&app->execution_context);
    free(app);
}

bool mini_sql_app_run_sql(MiniSqlApp *app, const char *sql, char *error_buf, size_t error_size) {
    return sql_processor_process(&app->sql_processor, sql, &app->execution_context, error_buf, error_size);
}

bool mini_sql_app_run_file(MiniSqlApp *app, const char *path, char *error_buf, size_t error_size) {
    char *sql_text = NULL;
    bool ok;

    if (!read_file_all(path, &sql_text, error_buf, error_size)) {
        return false;
    }

    ok = mini_sql_app_run_sql(app, sql_text, error_buf, error_size);
    free(sql_text);
    return ok;
}
