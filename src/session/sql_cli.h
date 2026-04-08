#ifndef SQL_CLI_H
#define SQL_CLI_H

#include "mini_sql.h"

/*
 * 인터랙티브 SQL CLI 루프를 실행한다.
 * 한 줄 또는 여러 줄 입력을 모아 세미콜론 단위 SqlInput 으로 만든 뒤
 * SqlSession 에게 실행을 위임한다.
 */
bool run_sql_cli(SqlSession *session, ErrorContext *err);

#endif
