#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "storage.h"


/* query.sql 파일 전체를 한 문자열로 읽어 파싱할 준비를 하는 함수이다. */
static int read_query_file(const char *path, char *sql, int size) {
    FILE *file = fopen(path, "r");
    char line[256];

    if (file == NULL) {
        return 0;
    }

    sql[0] = '\0';
    while (fgets(line, sizeof(line), file) != NULL) {
        if ((int)(strlen(sql) + strlen(line) + 2) >= size) {
            fclose(file);
            return 0;
        }
        strcat(sql, line);
        strcat(sql, " ");
    }

    fclose(file);
    return 1;
}

/* 파싱된 Query 정보를 보고 저장 처리인지 조회 처리인지 실행한다. */
static int execute_query(const Query *query) {
    if (query->type == QUERY_INSERT) {
        return append_user(query);
    }
    return select_users(query);
}

/* main은 파일 읽기 -> 파싱 -> 실행 순서만 관리하는 함수이다. */
int main(void) {
    char sql[1024];
    Query query;

    if (!read_query_file("query.sql", sql, sizeof(sql))) {
        printf("failed to read query.sql\n");
        return 1;
    }

    if (!parse_query(sql, &query)) {
        printf("failed to parse query\n");
        return 1;
    }

    if (!execute_query(&query)) {
        printf("failed to execute query\n");
        return 1;
    }

    return 0;
}
