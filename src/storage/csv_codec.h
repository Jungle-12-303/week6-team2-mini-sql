#ifndef CSV_CODEC_H
#define CSV_CODEC_H

#include "mini_sql.h"

/*
 * csv_codec.h / csv_codec.c 는 CSV 직렬화·역직렬화만 담당한다.
 * 테이블 경로와 스키마 메타데이터는 storage_path 와 schema_catalog 가 맡는다.
 */

/* 같은 버퍼 안에서 앞뒤 공백을 제거하고, 잘린 시작 위치를 반환한다. */
char *trim_in_place(char *text);
/* CSV 한 줄을 필드 배열로 파싱한다. */
bool parse_csv_line(const char *line, char ***out_fields, size_t *out_count, ErrorContext *err);
/* 필드 배열을 CSV 규칙에 맞춰 한 줄로 저장한다. */
bool write_csv_row(FILE *file, char **fields, size_t field_count, ErrorContext *err);

#endif
