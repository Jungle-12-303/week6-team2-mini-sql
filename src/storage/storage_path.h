#ifndef STORAGE_PATH_H
#define STORAGE_PATH_H

#include "mini_sql.h"

/* schema.table 형식을 실제 파일 경로용 schema/table 형식으로 바꾼다. */
char *build_table_path(const char *db_path, const char *table_name, const char *extension);
/* 파일 경로에 필요한 상위 디렉터리를 미리 만든다. */
bool ensure_parent_directories(const char *path, ErrorContext *err);

#endif
