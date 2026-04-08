#include "storage/storage_path.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * storage_path.c 는 "테이블 이름 <-> 파일 경로" 변환과 디렉터리 준비만 담당한다.
 * CSV 규칙이나 스키마 포맷은 다른 모듈이 맡고, 이 파일은 경로 문제만 분리한다.
 */

/* schema.table 형식을 파일 경로용 schema/table 형식으로 바꾼다. */
static char *normalize_table_name(const char *table_name) {
    char *normalized = msql_strdup(table_name);
    char *cursor;

    if (normalized == NULL) {
        return NULL;
    }

    for (cursor = normalized; *cursor != '\0'; ++cursor) {
        if (*cursor == '.') {
            *cursor = '/';
        }
    }

    return normalized;
}

/* DB 루트 + 정규화된 테이블 이름 + 확장자를 합쳐 실제 파일 경로를 만든다. */
char *build_table_path(const char *db_path, const char *table_name, const char *extension) {
    char *normalized = normalize_table_name(table_name);
    size_t length;
    char *path;

    if (normalized == NULL) {
        return NULL;
    }

    length = strlen(db_path) + 1U + strlen(normalized) + strlen(extension);
    path = malloc(length + 1U);
    if (path == NULL) {
        free(normalized);
        return NULL;
    }

    snprintf(path, length + 1U, "%s/%s%s", db_path, normalized, extension);
    free(normalized);
    return path;
}

/* 파일을 만들기 전에 필요한 부모 디렉터리를 순서대로 생성한다. */
bool ensure_parent_directories(const char *path, ErrorContext *err) {
    char *mutable_path = msql_strdup(path);
    char *cursor;

    if (mutable_path == NULL) {
        set_error(err, "디렉터리를 만드는 중 메모리가 부족합니다");
        return false;
    }

    for (cursor = mutable_path + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(mutable_path, 0755) != 0 && errno != EEXIST) {
                set_error(err, "디렉터리를 만들지 못했습니다 %s: %s", mutable_path, strerror(errno));
                free(mutable_path);
                return false;
            }
            *cursor = '/';
        }
    }

    free(mutable_path);
    return true;
}
