#include "storage_engine.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct FileStorageConfig {
    char *db_path;
} FileStorageConfig;

static FileStorageConfig *get_file_storage_config(StorageEngine *engine) {
    return (FileStorageConfig *) engine->impl;
}

static bool file_storage_load_schema(StorageEngine *engine, const char *table_name, TableSchema *schema,
                                     char *error_buf, size_t error_size) {
    FileStorageConfig *config = get_file_storage_config(engine);
    return load_table_schema(config->db_path, table_name, schema, error_buf, error_size);
}

static bool file_storage_save_schema(StorageEngine *engine, const char *table_name, const TableSchema *schema,
                                     char *error_buf, size_t error_size) {
    FileStorageConfig *config = get_file_storage_config(engine);
    char *schema_path = build_table_path(config->db_path, table_name, ".schema");
    FILE *schema_file = NULL;
    bool ok = false;

    if (schema_path == NULL) {
        set_error(error_buf, error_size, "out of memory while resolving schema path");
        return false;
    }

    if (!ensure_parent_directories(schema_path, error_buf, error_size)) {
        free(schema_path);
        return false;
    }

    schema_file = fopen(schema_path, "w");
    if (schema_file == NULL) {
        set_error(error_buf, error_size, "failed to open schema file %s: %s", schema_path, strerror(errno));
        free(schema_path);
        return false;
    }

    ok = write_csv_row(schema_file, schema->columns, schema->column_count, error_buf, error_size);
    fclose(schema_file);
    free(schema_path);
    return ok;
}

static bool file_storage_append_row(StorageEngine *engine, const char *table_name, char **fields, size_t field_count,
                                    char *error_buf, size_t error_size) {
    FileStorageConfig *config = get_file_storage_config(engine);
    char *data_path = build_table_path(config->db_path, table_name, ".data");
    FILE *data_file = NULL;
    bool ok = false;

    if (data_path == NULL) {
        set_error(error_buf, error_size, "out of memory while resolving table path");
        return false;
    }

    if (!ensure_parent_directories(data_path, error_buf, error_size)) {
        free(data_path);
        return false;
    }

    data_file = fopen(data_path, "a");
    if (data_file == NULL) {
        set_error(error_buf, error_size, "failed to open data file %s: %s", data_path, strerror(errno));
        free(data_path);
        return false;
    }

    ok = write_csv_row(data_file, fields, field_count, error_buf, error_size);
    fclose(data_file);
    free(data_path);
    return ok;
}

static bool file_storage_scan_rows(StorageEngine *engine, const char *table_name, RowVisitorFn visitor, void *user_data,
                                   char *error_buf, size_t error_size) {
    FileStorageConfig *config = get_file_storage_config(engine);
    char *data_path = build_table_path(config->db_path, table_name, ".data");
    FILE *data_file = NULL;
    bool ok = true;

    if (data_path == NULL) {
        set_error(error_buf, error_size, "out of memory while resolving table path");
        return false;
    }

    data_file = fopen(data_path, "r");
    if (data_file == NULL) {
        if (errno == ENOENT) {
            free(data_path);
            return true;
        }
        set_error(error_buf, error_size, "failed to open data file %s: %s", data_path, strerror(errno));
        free(data_path);
        return false;
    }

    while (true) {
        char *line = read_stream_line(data_file);
        char *trimmed;
        char **fields = NULL;
        size_t field_count = 0U;

        if (line == NULL) {
            break;
        }

        trimmed = trim_in_place(line);
        if (*trimmed == '\0') {
            free(line);
            continue;
        }

        if (!parse_csv_line(trimmed, &fields, &field_count, error_buf, error_size)) {
            free(line);
            ok = false;
            break;
        }

        ok = visitor(fields, field_count, user_data, error_buf, error_size);
        free_string_array(fields, field_count);
        free(line);

        if (!ok) {
            break;
        }
    }

    fclose(data_file);
    free(data_path);
    return ok;
}

static void file_storage_destroy(StorageEngine *engine) {
    FileStorageConfig *config = get_file_storage_config(engine);

    if (config != NULL) {
        free(config->db_path);
        free(config);
    }

    engine->ops = NULL;
    engine->impl = NULL;
}

static const StorageEngineOps FILE_STORAGE_OPS = {
    file_storage_load_schema,
    file_storage_save_schema,
    file_storage_append_row,
    file_storage_scan_rows,
    file_storage_destroy
};

bool storage_engine_create_file(StorageEngine *engine, const char *db_path, char *error_buf, size_t error_size) {
    FileStorageConfig *config;

    if (engine == NULL || db_path == NULL) {
        set_error(error_buf, error_size, "invalid storage engine configuration");
        return false;
    }

    config = calloc(1U, sizeof(*config));
    if (config == NULL) {
        set_error(error_buf, error_size, "out of memory while creating storage engine");
        return false;
    }

    config->db_path = msql_strdup(db_path);
    if (config->db_path == NULL) {
        free(config);
        set_error(error_buf, error_size, "out of memory while creating storage engine");
        return false;
    }

    engine->ops = &FILE_STORAGE_OPS;
    engine->impl = config;
    return true;
}

bool storage_engine_load_schema(StorageEngine *engine, const char *table_name, TableSchema *schema,
                                char *error_buf, size_t error_size) {
    return engine->ops->load_schema(engine, table_name, schema, error_buf, error_size);
}

bool storage_engine_save_schema(StorageEngine *engine, const char *table_name, const TableSchema *schema,
                                char *error_buf, size_t error_size) {
    return engine->ops->save_schema(engine, table_name, schema, error_buf, error_size);
}

bool storage_engine_append_row(StorageEngine *engine, const char *table_name, char **fields, size_t field_count,
                               char *error_buf, size_t error_size) {
    return engine->ops->append_row(engine, table_name, fields, field_count, error_buf, error_size);
}

bool storage_engine_scan_rows(StorageEngine *engine, const char *table_name, RowVisitorFn visitor, void *user_data,
                              char *error_buf, size_t error_size) {
    return engine->ops->scan_rows(engine, table_name, visitor, user_data, error_buf, error_size);
}

void storage_engine_destroy(StorageEngine *engine) {
    if (engine == NULL || engine->ops == NULL || engine->ops->destroy == NULL) {
        return;
    }
    engine->ops->destroy(engine);
}
