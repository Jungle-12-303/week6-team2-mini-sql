#include "storage/storage_engine.h"
#include "storage/csv_codec.h"
#include "storage/storage_path.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * 이 파일은 StorageEngine 인터페이스의 "파일 기반 구현체"다.
 * SQL 계층은 StorageEngine 함수만 호출하고,
 * 실제로 .schema/.data 파일을 어떻게 읽고 쓰는지는 이 구현이 책임진다.
 */

/* 파일 기반 구현이 유지해야 하는 최소 설정은 DB 루트 경로 하나다. */
typedef struct FileStorageConfig {
    char *db_path;
    char *cached_table_name;
    CatalogSchema cached_schema;
} FileStorageConfig;

/* 파싱된 한 행을 처리하는 파일 내부 공통 콜백 타입이다. */
typedef bool (*ParsedRowHandlerFn)(char **fields, size_t field_count, void *user_data, ErrorContext *err);

/* DELETE 재작성 과정에서 필요한 상태를 묶는다. */
typedef struct DeleteRewriteState {
    const CatalogSchema *schema;
    RowMatchFn matcher;
    void *user_data;
    FILE *temp_file;
    size_t deleted_count;
} DeleteRewriteState;

/* scan_rows 에서 visitor 와 user_data 를 함께 넘기기 위한 상태 묶음이다. */
typedef struct ScanVisitorState {
    const CatalogSchema *schema;
    RowVisitorFn visitor;
    void *user_data;
} ScanVisitorState;

typedef bool (*StorageEngineCreateFn)(StorageEngine *engine, const char *db_path, ErrorContext *err);

typedef struct StorageEngineFactoryEntry {
    StorageEngineKind kind;
    const char *display_name;
    StorageEngineCreateFn create;
} StorageEngineFactoryEntry;

/* engine->impl 을 파일 구현체 설정 구조체로 캐스팅한다. */
static FileStorageConfig *get_file_storage_config(StorageEngine *engine) {
    /* StorageEngine 의 impl 포인터를 파일 구현체 설정 구조체로 본다. */
    return (FileStorageConfig *) engine->impl;
}

/* 현재 캐시된 스키마를 비운다. */
static void clear_schema_cache(FileStorageConfig *config) {
    /* 캐시된 테이블 이름 문자열을 정리한다. */
    free(config->cached_table_name);
    /* 포인터를 비워 재사용 실수를 막는다. */
    config->cached_table_name = NULL;
    /* 캐시된 스키마 내부 문자열과 배열도 함께 정리한다. */
    catalog_free_schema(&config->cached_schema);
}

/* 로드한 스키마를 내부 캐시에 복사해 다음 요청에서 재사용할 수 있게 한다. */
static void try_cache_schema(FileStorageConfig *config, const char *table_name, const CatalogSchema *schema) {
    /* 캐시 복사 실패를 무시하기 위한 임시 에러 버퍼다. */
    ErrorContext ignored = {0};
    /* 캐시에 넣을 스키마 복사본이다. */
    CatalogSchema schema_copy = {0};
    /* 캐시에 넣을 테이블 이름 복사본이다. */
    char *table_name_copy;

    /* 스키마 복사에 실패하면 캐시를 갱신하지 않고 끝낸다. */
    if (!catalog_copy_schema(schema, &schema_copy, &ignored)) {
        return;
    }

    /* 테이블 이름도 캐시에 넣기 위해 복사한다. */
    table_name_copy = msql_strdup(table_name);
    /* 이름 복사에 실패하면 스키마 복사본을 정리하고 끝낸다. */
    if (table_name_copy == NULL) {
        catalog_free_schema(&schema_copy);
        return;
    }

    /* 기존 캐시를 먼저 비운다. */
    clear_schema_cache(config);
    /* 새 테이블 이름 복사본을 저장한다. */
    config->cached_table_name = table_name_copy;
    /* 새 스키마 복사본을 저장한다. */
    config->cached_schema = schema_copy;
}

static bool convert_storage_fields_to_logical(const CatalogSchema *schema, char **storage_fields,
                                              size_t field_count, char ***out_logical_fields,
                                              ErrorContext *err) {
    return catalog_build_logical_row(schema, storage_fields, field_count, out_logical_fields, err);
}

static bool convert_logical_fields_to_storage(const CatalogSchema *schema, char **logical_fields,
                                              size_t field_count, char ***out_storage_fields,
                                              ErrorContext *err) {
    return catalog_build_storage_row(schema, logical_fields, field_count, out_storage_fields, err);
}

/*
 * data 파일을 한 줄씩 읽어 CSV 행으로 복원한 뒤 handler 로 넘긴다.
 * scan/delete 모두 같은 "줄 읽기 -> trim -> parse -> callback" 흐름을 공유하므로
 * 이 루틴 하나로 재사용한다.
 */
static bool for_each_data_row(FILE *data_file, ParsedRowHandlerFn handler, void *user_data, ErrorContext *err) {
    /* 파일 끝을 만날 때까지 한 줄씩 읽는다. */
    while (true) {
        /* 현재 줄 문자열을 읽어 온다. */
        char *line = read_stream_line(data_file);
        /* 좌우 공백을 제거한 실제 내용 시작 위치다. */
        char *trimmed;
        /* CSV 파싱 후 얻은 필드 배열이다. */
        char **fields = NULL;
        /* CSV 필드 개수다. */
        size_t field_count = 0U;
        /* handler 수행 성공 여부다. */
        bool ok;

        /* 더 읽을 줄이 없으면 순회를 정상 종료한다. */
        if (line == NULL) {
            return true;
        }

        /* 줄 양끝 공백을 제거한다. */
        trimmed = trim_in_place(line);
        /* 빈 줄이면 그냥 버리고 다음 줄로 넘어간다. */
        if (*trimmed == '\0') {
            free(line);
            continue;
        }

        /* CSV 한 줄을 필드 배열로 복원한다. */
        if (!parse_csv_line(trimmed, &fields, &field_count, err)) {
            free(line);
            return false;
        }

        /* 복원된 행을 콜백으로 넘겨 실제 처리를 수행한다. */
        ok = handler(fields, field_count, user_data, err);
        /* 필드 배열을 정리한다. */
        free_string_array(fields, field_count);
        /* 원본 줄 문자열도 정리한다. */
        free(line);

        /* 콜백이 실패했으면 즉시 중단한다. */
        if (!ok) {
            return false;
        }
    }
}

/* scan_rows 는 visitor 를 그대로 재사용할 수 있으므로 얇은 어댑터만 둔다. */
static bool visit_scanned_row(char **fields, size_t field_count, void *user_data, ErrorContext *err) {
    /* 사용자 visitor 와 상태를 함께 담은 구조체를 꺼낸다. */
    ScanVisitorState *state = (ScanVisitorState *) user_data;
    char **logical_fields = NULL;
    bool ok;

    if (!convert_storage_fields_to_logical(state->schema, fields, field_count, &logical_fields, err)) {
        return false;
    }

    /* 실제 visitor 는 항상 사용자 스키마 순서의 행만 보게 한다. */
    ok = state->visitor(logical_fields, field_count, state->user_data, err);
    free_string_array(logical_fields, field_count);
    return ok;
}

/* DELETE 에서 행을 지울지 판단하고, 남길 행만 임시 파일에 다시 쓴다. */
static bool rewrite_or_delete_row(char **fields, size_t field_count, void *user_data, ErrorContext *err) {
    /* 삭제 판정과 재작성에 필요한 상태를 꺼낸다. */
    DeleteRewriteState *state = (DeleteRewriteState *) user_data;
    /* 현재 행을 삭제할지 여부다. */
    bool should_delete = false;
    char **logical_fields = NULL;
    bool ok;

    /* 현재 행이 삭제 대상인지 matcher 에게 물어본다. */
    if (!convert_storage_fields_to_logical(state->schema, fields, field_count, &logical_fields, err)) {
        return false;
    }
    ok = state->matcher(logical_fields, field_count, state->user_data, &should_delete, err);
    free_string_array(logical_fields, field_count);
    if (!ok) {
        return false;
    }

    /* 삭제 대상이면 카운트만 늘리고 파일에는 쓰지 않는다. */
    if (should_delete) {
        state->deleted_count += 1U;
        return true;
    }

    /* 삭제 대상이 아니면 임시 파일에 그대로 다시 기록한다. */
    return write_csv_row(state->temp_file, fields, field_count, err);
}

/* .schema 파일을 읽어 CatalogSchema 로 복원한다. */
static bool file_storage_load_schema(StorageEngine *engine, const char *table_name, CatalogSchema *schema,
                                     ErrorContext *err) {
    /* 현재 파일 저장 엔진 설정을 꺼낸다. */
    FileStorageConfig *config = get_file_storage_config(engine);

    /* 같은 테이블 스키마가 캐시에 있으면 파일을 다시 읽지 않고 복사본을 준다. */
    if (config->cached_table_name != NULL && strings_equal_ci(config->cached_table_name, table_name)) {
        return catalog_copy_schema(&config->cached_schema, schema, err);
    }

    /* 캐시에 없으면 실제 .schema 파일에서 스키마를 읽는다. */
    if (!catalog_load_schema(config->db_path, table_name, schema, err)) {
        return false;
    }

    /* 방금 읽은 스키마를 다음 요청 재사용을 위해 캐시에 넣는다. */
    try_cache_schema(config, table_name, schema);
    /* 스키마 로드 성공을 반환한다. */
    return true;
}

/*
 * CREATE TABLE 의 물리 구현이다.
 * 1. schema/data 파일 경로를 만든다.
 * 2. 이미 존재하는지 확인한다.
 * 3. .schema 를 저장한다.
 * 4. 빈 .data 파일을 만든다.
 */
static bool file_storage_create_table(StorageEngine *engine, const char *table_name, const CatalogSchema *schema,
                                      ErrorContext *err) {
    /* 현재 파일 저장 엔진 설정을 꺼낸다. */
    FileStorageConfig *config = get_file_storage_config(engine);
    /* .schema 파일 경로다. */
    char *schema_path = build_table_path(config->db_path, table_name, ".schema");
    /* .data 파일 경로다. */
    char *data_path = build_table_path(config->db_path, table_name, ".data");
    /* 빈 .data 파일을 만들 때 쓸 FILE 포인터다. */
    FILE *data_file = NULL;

    /* 경로 문자열 생성에 실패하면 더 진행할 수 없다. */
    if (schema_path == NULL || data_path == NULL) {
        free(schema_path);
        free(data_path);
        set_error(err, "테이블을 만드는 중 메모리가 부족합니다");
        return false;
    }

    /* 이미 schema 나 data 파일이 있으면 기존 테이블로 본다. */
    if (access(schema_path, F_OK) == 0 || access(data_path, F_OK) == 0) {
        free(schema_path);
        free(data_path);
        set_error(err, "테이블 '%s'가 이미 존재합니다", table_name);
        return false;
    }

    /* 스키마 메타데이터를 .schema 파일로 저장한다. */
    if (!catalog_save_schema(config->db_path, table_name, schema, err)) {
        free(schema_path);
        free(data_path);
        return false;
    }

    /* .data 파일의 상위 디렉터리가 없으면 먼저 만든다. */
    if (!ensure_parent_directories(data_path, err)) {
        unlink(schema_path);
        free(schema_path);
        free(data_path);
        return false;
    }

    /* 비어 있는 data 파일을 생성한다. */
    data_file = fopen(data_path, "w");
    /* 파일 생성 실패 시 방금 만든 schema 파일도 되돌린다. */
    if (data_file == NULL) {
        set_error(err, "데이터 파일을 만들지 못했습니다 %s: %s", data_path, strerror(errno));
        unlink(schema_path);
        free(schema_path);
        free(data_path);
        return false;
    }

    /* 빈 data 파일을 닫는다. */
    fclose(data_file);
    /* 생성한 스키마를 캐시에 넣어 이후 요청에서 재사용한다. */
    try_cache_schema(config, table_name, schema);
    /* 경로 문자열들을 정리한다. */
    free(schema_path);
    free(data_path);
    /* CREATE TABLE 물리 구현 성공을 반환한다. */
    return true;
}

/*
 * DROP TABLE 의 물리 구현이다.
 * schema/data 파일을 모두 지우고, 둘 다 없으면 "없는 테이블" 에러로 처리한다.
 */
static bool file_storage_drop_table(StorageEngine *engine, const char *table_name,
                                    ErrorContext *err) {
    /* 현재 파일 저장 엔진 설정을 꺼낸다. */
    FileStorageConfig *config = get_file_storage_config(engine);
    /* .schema 파일 경로다. */
    char *schema_path = build_table_path(config->db_path, table_name, ".schema");
    /* .data 파일 경로다. */
    char *data_path = build_table_path(config->db_path, table_name, ".data");
    /* 둘 중 하나라도 실제로 지웠는지 기록한다. */
    bool removed_any = false;

    /* 경로 생성에 실패하면 더 진행할 수 없다. */
    if (schema_path == NULL || data_path == NULL) {
        free(schema_path);
        free(data_path);
        set_error(err, "테이블을 삭제하는 중 메모리가 부족합니다");
        return false;
    }

    /* schema 파일을 삭제해 본다. */
    if (unlink(schema_path) == 0) {
        removed_any = true;
    /* 파일이 없던 경우는 무시하고, 다른 실패만 오류로 본다. */
    } else if (errno != ENOENT) {
        set_error(err, "스키마 파일을 삭제하지 못했습니다 %s: %s", schema_path, strerror(errno));
        free(schema_path);
        free(data_path);
        return false;
    }

    /* data 파일도 같은 방식으로 삭제한다. */
    if (unlink(data_path) == 0) {
        removed_any = true;
    } else if (errno != ENOENT) {
        set_error(err, "데이터 파일을 삭제하지 못했습니다 %s: %s", data_path, strerror(errno));
        free(schema_path);
        free(data_path);
        return false;
    }

    free(schema_path);
    free(data_path);
    /* 지운 테이블이 캐시에 있었다면 캐시도 함께 비운다. */
    if (config->cached_table_name != NULL && strings_equal_ci(config->cached_table_name, table_name)) {
        clear_schema_cache(config);
    }

    /* schema/data 둘 다 없었다면 존재하지 않는 테이블로 본다. */
    if (!removed_any) {
        set_error(err, "테이블 '%s'가 존재하지 않습니다", table_name);
        return false;
    }

    /* DROP TABLE 물리 구현 성공을 반환한다. */
    return true;
}

/* INSERT 의 물리 구현이다. 이미 정렬된 row 필드를 .data 끝에 한 줄 append 한다. */
static bool file_storage_append_row(StorageEngine *engine, const char *table_name, char **fields, size_t field_count,
                                    ErrorContext *err) {
    /* 현재 파일 저장 엔진 설정을 꺼낸다. */
    FileStorageConfig *config = get_file_storage_config(engine);
    CatalogSchema schema = {0};
    char **storage_fields = NULL;
    /* .data 파일 경로다. */
    char *data_path = build_table_path(config->db_path, table_name, ".data");
    /* append 모드로 열 data 파일 포인터다. */
    FILE *data_file = NULL;
    /* 실제 CSV 기록 성공 여부다. */
    bool ok = false;

    /* 경로 생성에 실패하면 더 진행할 수 없다. */
    if (data_path == NULL) {
        set_error(err, "테이블 경로를 만드는 중 메모리가 부족합니다");
        return false;
    }

    if (!file_storage_load_schema(engine, table_name, &schema, err)) {
        free(data_path);
        return false;
    }
    if (!convert_logical_fields_to_storage(&schema, fields, field_count, &storage_fields, err)) {
        catalog_free_schema(&schema);
        free(data_path);
        return false;
    }

    /* 상위 디렉터리가 없으면 먼저 만든다. */
    if (!ensure_parent_directories(data_path, err)) {
        free_string_array(storage_fields, field_count);
        catalog_free_schema(&schema);
        free(data_path);
        return false;
    }

    /* data 파일을 append 모드로 연다. */
    data_file = fopen(data_path, "a");
    /* 열기에 실패하면 즉시 오류를 반환한다. */
    if (data_file == NULL) {
        set_error(err, "데이터 파일을 열지 못했습니다 %s: %s", data_path, strerror(errno));
        free_string_array(storage_fields, field_count);
        catalog_free_schema(&schema);
        free(data_path);
        return false;
    }

    /* 논리 컬럼 순서를 내부 저장 슬롯 순서로 바꿔 CSV 한 줄로 저장한다. */
    ok = write_csv_row(data_file, storage_fields, field_count, err);
    /* 파일을 닫는다. */
    fclose(data_file);
    free_string_array(storage_fields, field_count);
    catalog_free_schema(&schema);
    /* 경로 문자열을 정리한다. */
    free(data_path);
    /* 최종 기록 성공 여부를 반환한다. */
    return ok;
}

/*
 * SELECT 를 위한 행 순회 구현이다.
 * 이 함수는 "행을 어떻게 활용할지"를 모르고, 각 행을 visitor 콜백으로 넘기기만 한다.
 */
static bool file_storage_scan_rows(StorageEngine *engine, const char *table_name, RowVisitorFn visitor, void *user_data,
                                   ErrorContext *err) {
    /* 현재 파일 저장 엔진 설정을 꺼낸다. */
    FileStorageConfig *config = get_file_storage_config(engine);
    CatalogSchema schema = {0};
    /* .data 파일 경로다. */
    char *data_path = build_table_path(config->db_path, table_name, ".data");
    /* 읽기 모드로 열 data 파일 포인터다. */
    FILE *data_file = NULL;
    /* visitor 와 user_data 를 함께 넘길 상태 구조체다. */
    ScanVisitorState scan_state;
    /* 전체 행 순회 성공 여부다. */
    bool ok;

    /* 경로 생성에 실패하면 중단한다. */
    if (data_path == NULL) {
        set_error(err, "테이블 경로를 만드는 중 메모리가 부족합니다");
        return false;
    }

    if (!file_storage_load_schema(engine, table_name, &schema, err)) {
        free(data_path);
        return false;
    }

    /* data 파일을 읽기 모드로 연다. */
    data_file = fopen(data_path, "r");
    if (data_file == NULL) {
        /* 파일이 아직 없으면 빈 테이블처럼 취급한다. */
        if (errno == ENOENT) {
            catalog_free_schema(&schema);
            free(data_path);
            return true;
        }
        /* 그 외 실패는 실제 오류다. */
        set_error(err, "데이터 파일을 열지 못했습니다 %s: %s", data_path, strerror(errno));
        catalog_free_schema(&schema);
        free(data_path);
        return false;
    }

    /* 실제 visitor 함수 포인터를 상태에 저장한다. */
    scan_state.schema = &schema;
    scan_state.visitor = visitor;
    /* visitor 가 사용할 사용자 상태도 저장한다. */
    scan_state.user_data = user_data;
    /* 공통 행 순회 루틴으로 각 CSV 행을 visitor 에 전달한다. */
    ok = for_each_data_row(data_file, visit_scanned_row, &scan_state, err);

    /* 파일을 닫는다. */
    fclose(data_file);
    /* 경로 문자열을 정리한다. */
    free(data_path);
    catalog_free_schema(&schema);
    /* 전체 순회 성공 여부를 반환한다. */
    return ok;
}

/*
 * DELETE 의 물리 구현이다.
 * 기존 .data 를 읽으면서 지우지 않을 행만 임시 파일에 다시 쓰고,
 * 마지막에 임시 파일을 원본 파일로 교체한다.
 */
static bool file_storage_delete_rows(StorageEngine *engine, const char *table_name, RowMatchFn matcher, void *user_data,
                                     size_t *out_deleted_count, ErrorContext *err) {
    /* 현재 파일 저장 엔진 설정을 꺼낸다. */
    FileStorageConfig *config = get_file_storage_config(engine);
    CatalogSchema schema = {0};
    /* 원본 .data 파일 경로다. */
    char *data_path = build_table_path(config->db_path, table_name, ".data");
    /* 임시 파일 경로다. */
    char *temp_path = NULL;
    /* 원본 파일 포인터다. */
    FILE *data_file = NULL;
    /* 재작성용 임시 파일 포인터다. */
    FILE *temp_file = NULL;
    /* 삭제/재작성 콜백이 사용할 상태 구조체다. */
    DeleteRewriteState rewrite_state = {0};
    /* 전체 삭제 작업 성공 여부다. */
    bool ok = false;

    /* 경로 생성에 실패하면 중단한다. */
    if (data_path == NULL) {
        set_error(err, "테이블 경로를 만드는 중 메모리가 부족합니다");
        return false;
    }

    if (!file_storage_load_schema(engine, table_name, &schema, err)) {
        free(data_path);
        return false;
    }

    /* 원본 data 파일을 읽기 모드로 연다. */
    data_file = fopen(data_path, "r");
    if (data_file == NULL) {
        /* 파일이 아직 없으면 삭제할 행도 0개다. */
        if (errno == ENOENT) {
            catalog_free_schema(&schema);
            free(data_path);
            if (out_deleted_count != NULL) {
                *out_deleted_count = 0U;
            }
            return true;
        }
        /* 그 외 실패는 실제 오류다. */
        set_error(err, "데이터 파일을 열지 못했습니다 %s: %s", data_path, strerror(errno));
        catalog_free_schema(&schema);
        free(data_path);
        return false;
    }

    /* 임시 파일 경로는 "<원본경로>.tmp" 형식으로 만든다. */
    temp_path = malloc(strlen(data_path) + 5U);
    if (temp_path == NULL) {
        fclose(data_file);
        catalog_free_schema(&schema);
        free(data_path);
        set_error(err, "행을 삭제하는 중 메모리가 부족합니다");
        return false;
    }
    /* 임시 파일 전체 경로 문자열을 완성한다. */
    sprintf(temp_path, "%s.tmp", data_path);

    /* 임시 파일을 쓰기 모드로 연다. */
    temp_file = fopen(temp_path, "w");
    if (temp_file == NULL) {
        set_error(err, "임시 파일을 열지 못했습니다 %s: %s", temp_path, strerror(errno));
        fclose(data_file);
        catalog_free_schema(&schema);
        free(data_path);
        free(temp_path);
        return false;
    }

    rewrite_state.schema = &schema;
    /* 삭제 판정 함수 포인터를 상태에 저장한다. */
    rewrite_state.matcher = matcher;
    /* 삭제 판정이 사용할 사용자 상태를 저장한다. */
    rewrite_state.user_data = user_data;
    /* 남길 행을 기록할 임시 파일 포인터를 저장한다. */
    rewrite_state.temp_file = temp_file;
    /* 원본 파일을 순회하며 삭제 대상이 아닌 행만 임시 파일에 다시 쓴다. */
    ok = for_each_data_row(data_file, rewrite_or_delete_row, &rewrite_state, err);

    /* 원본 파일을 닫는다. */
    fclose(data_file);
    /* 임시 파일도 닫는다. */
    fclose(temp_file);

    /* 재작성 중 오류가 났으면 임시 파일을 삭제하고 끝낸다. */
    if (!ok) {
        unlink(temp_path);
        catalog_free_schema(&schema);
        free(data_path);
        free(temp_path);
        return false;
    }

    /* 재작성된 임시 파일을 원본 파일 이름으로 교체한다. */
    if (rename(temp_path, data_path) != 0) {
        set_error(err, "데이터 파일을 교체하지 못했습니다 %s: %s", data_path, strerror(errno));
        unlink(temp_path);
        catalog_free_schema(&schema);
        free(data_path);
        free(temp_path);
        return false;
    }

    /* 호출자가 원하면 실제 삭제 행 개수를 돌려준다. */
    if (out_deleted_count != NULL) {
        *out_deleted_count = rewrite_state.deleted_count;
    }

    /* 경로 문자열들을 정리한다. */
    catalog_free_schema(&schema);
    free(data_path);
    free(temp_path);
    /* DELETE 물리 구현 성공을 반환한다. */
    return true;
}

/* 파일 기반 구현체가 잡고 있던 설정 메모리를 정리한다. */
static void file_storage_destroy(StorageEngine *engine) {
    /* 파일 구현체 내부 설정 구조체를 꺼낸다. */
    FileStorageConfig *config = get_file_storage_config(engine);

    /* 설정 구조체가 있으면 그 내부 자원부터 정리한다. */
    if (config != NULL) {
        free(config->db_path);
        clear_schema_cache(config);
        free(config);
    }

    /* 파괴 후 인터페이스 포인터를 비운다. */
    engine->ops = NULL;
    /* 구현체 상태 포인터도 비운다. */
    engine->impl = NULL;
}

/* 파일 구현체가 StorageEngine 인터페이스에 연결하는 함수 테이블이다. */
static const StorageEngineOps FILE_STORAGE_OPS = {
    file_storage_load_schema,
    file_storage_create_table,
    file_storage_drop_table,
    file_storage_append_row,
    file_storage_scan_rows,
    file_storage_delete_rows,
    file_storage_destroy
};

/* CSV 파일 구현체를 초기화한다. */
static bool create_csv_storage_engine(StorageEngine *engine, const char *db_path, ErrorContext *err) {
    /* 새 파일 저장 엔진 설정 구조체다. */
    FileStorageConfig *config;

    /* 출력 포인터와 DB 경로는 필수 입력이다. */
    if (engine == NULL || db_path == NULL) {
        set_error(err, "저장 엔진 설정이 올바르지 않습니다");
        return false;
    }

    /* 설정 구조체를 0으로 초기화된 상태로 할당한다. */
    config = calloc(1U, sizeof(*config));
    /* 할당 실패 시 더 진행할 수 없다. */
    if (config == NULL) {
        set_error(err, "저장 엔진을 만드는 중 메모리가 부족합니다");
        return false;
    }

    /* DB 루트 경로를 설정 구조체 안에 복사해 저장한다. */
    config->db_path = msql_strdup(db_path);
    /* 경로 복사 실패 시 설정 구조체를 정리하고 끝낸다. */
    if (config->db_path == NULL) {
        free(config);
        set_error(err, "저장 엔진을 만드는 중 메모리가 부족합니다");
        return false;
    }

    /* 파일 저장 구현체용 함수 테이블을 연결한다. */
    engine->ops = &FILE_STORAGE_OPS;
    /* 구현체 전용 설정 구조체를 연결한다. */
    engine->impl = config;
    /* 저장 엔진 생성 성공을 반환한다. */
    return true;
}

static const StorageEngineFactoryEntry STORAGE_ENGINE_FACTORIES[] = {
    {STORAGE_ENGINE_CSV, "CSV", create_csv_storage_engine},
    {STORAGE_ENGINE_BINARY, "바이너리", NULL},
    {STORAGE_ENGINE_BPTREE, "B+Tree", NULL},
    {STORAGE_ENGINE_REMOTE, "원격", NULL}
};

static const StorageEngineFactoryEntry *find_storage_engine_factory(StorageEngineKind kind) {
    size_t i;

    for (i = 0; i < sizeof(STORAGE_ENGINE_FACTORIES) / sizeof(STORAGE_ENGINE_FACTORIES[0]); ++i) {
        if (STORAGE_ENGINE_FACTORIES[i].kind == kind) {
            return &STORAGE_ENGINE_FACTORIES[i];
        }
    }

    return NULL;
}

/* 외부에서 호출하는 저장 엔진 팩토리다. */
bool storage_engine_create(StorageEngine *engine, StorageEngineKind kind, const char *db_path,
                           ErrorContext *err) {
    const StorageEngineFactoryEntry *factory;

    factory = find_storage_engine_factory(kind);
    if (factory == NULL) {
        set_error(err, "알 수 없는 저장 엔진 종류입니다");
        return false;
    }

    if (factory->create == NULL) {
        set_error(err, "%s 저장 엔진은 아직 구현되지 않았습니다", factory->display_name);
        return false;
    }

    return factory->create(engine, db_path, err);
}

/* 아래 함수들은 저장 엔진 인터페이스 위임 계층이다. */
bool storage_engine_load_schema(StorageEngine *engine, const char *table_name, CatalogSchema *schema,
                                ErrorContext *err) {
    /* 현재 구현체의 load_schema 함수 포인터로 위임한다. */
    return engine->ops->load_schema(engine, table_name, schema, err);
}

bool storage_engine_create_table(StorageEngine *engine, const char *table_name, const CatalogSchema *schema,
                                 ErrorContext *err) {
    /* 현재 구현체의 create_table 함수 포인터로 위임한다. */
    return engine->ops->create_table(engine, table_name, schema, err);
}

bool storage_engine_drop_table(StorageEngine *engine, const char *table_name, ErrorContext *err) {
    /* 현재 구현체의 drop_table 함수 포인터로 위임한다. */
    return engine->ops->drop_table(engine, table_name, err);
}

bool storage_engine_append_row(StorageEngine *engine, const char *table_name, char **fields, size_t field_count,
                               ErrorContext *err) {
    /* 현재 구현체의 append_row 함수 포인터로 위임한다. */
    return engine->ops->append_row(engine, table_name, fields, field_count, err);
}

bool storage_engine_scan_rows(StorageEngine *engine, const char *table_name, RowVisitorFn visitor, void *user_data,
                              ErrorContext *err) {
    /* 현재 구현체의 scan_rows 함수 포인터로 위임한다. */
    return engine->ops->scan_rows(engine, table_name, visitor, user_data, err);
}

bool storage_engine_delete_rows(StorageEngine *engine, const char *table_name, RowMatchFn matcher, void *user_data,
                                size_t *out_deleted_count, ErrorContext *err) {
    /* 현재 구현체의 delete_rows 함수 포인터로 위임한다. */
    return engine->ops->delete_rows(engine, table_name, matcher, user_data, out_deleted_count, err);
}

/* 구현체가 무엇이든 destroy 포인터만 있으면 공통 방식으로 정리할 수 있다. */
void storage_engine_destroy(StorageEngine *engine) {
    /* 엔진 포인터나 destroy 함수가 없으면 정리할 수 없으므로 끝낸다. */
    if (engine == NULL || engine->ops == NULL || engine->ops->destroy == NULL) {
        return;
    }
    /* 구체 구현체가 무엇이든 destroy 함수 포인터만 호출한다. */
    engine->ops->destroy(engine);
}
