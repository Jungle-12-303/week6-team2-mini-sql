#ifndef SQL_FRONTEND_H
#define SQL_FRONTEND_H

#include "mini_sql.h"

/*
 * SqlFrontend 는 SQL 텍스트를 해석 가능한 StatementList 로 바꾸는 계층이다.
 *
 *   입력 텍스트
 *     -> tokenize
 *     -> parse
 *     -> StatementList(AST)
 */
typedef struct SqlFrontend {
    unsigned char reserved;
} SqlFrontend;

void sql_frontend_init(SqlFrontend *frontend);
bool sql_frontend_compile(const SqlFrontend *frontend, const char *sql,
                          StatementList *out_statements, ErrorContext *err);

#endif
