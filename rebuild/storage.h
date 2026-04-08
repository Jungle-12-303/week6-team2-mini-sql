#ifndef STORAGE_H
#define STORAGE_H

#include "common.h"

// 즉 헤더는:“이 모듈이 바깥에 공개하는 기능 목록”

int storage_append_row(const char *data_dir,
                       const Statement *stmt,
                       char *error,
                       size_t error_size);

int storage_select_all(const char *data_dir,
                       const Statement *stmt,
                       FILE *out,
                       char *error,
                       size_t error_size);

#endif
