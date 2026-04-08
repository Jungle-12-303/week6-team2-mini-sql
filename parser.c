#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

static void trim_sql(char *text) {
    int start = 0;
    int end = (int)strlen(text) - 1;

    while (text[start] && isspace((unsigned char)text[start])) {
        start++;
    }
    while (end >= start && isspace((unsigned char)text[end])) {
        text[end--] = '\0';
    }
    if (end >= start && text[end] == ';') {
        text[end--] = '\0';
    }
    while (end >= start && isspace((unsigned char)text[end])) {
        text[end--] = '\0';
    }
    if (start > 0) {
        memmove(text, text + start, strlen(text + start) + 1);
    }
}

static void trim_token(char *text) {
    char *start = text;
    char *end;

    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    end = text + strlen(text) - 1;
    while (end >= text && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
}

static int parse_selected_columns(char *columns, Query *query) {
    char *token = strtok(columns, ",");

    query->selected_column_count = 0;
    while (token != NULL) {
        if (query->selected_column_count >= 2) {
            return 0;
        }
        trim_token(token);
        if (strcmp(token, "id") != 0 &&
            strcmp(token, "name") != 0 &&
            strcmp(token, "age") != 0) {
            return 0;
        }
        strcpy(query->selected_columns[query->selected_column_count], token);
        query->selected_column_count++;
        token = strtok(NULL, ",");
    }

    return query->selected_column_count > 0;
}

int parse_query(const char *sql, Query *query) {
    char buffer[256];
    char *from_pos;
    char columns[64];

    memset(query, 0, sizeof(Query));
    strncpy(buffer, sql, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    trim_sql(buffer);

    if (strncmp(buffer, "INSERT INTO ", 12) == 0) {
        //strncmp는  buffer와 "INSERT INTO "의 처음 12글자를 비교하는 함수이다. --- IGNORE ---
        query->type = QUERY_INSERT;
        if (sscanf(buffer, "INSERT INTO %31s VALUES ( %d , '%31[^']' , %d )",
                   query->table_name, &query->id, query->name, &query->age) != 4) {
            return 0;
            //31자 이유는 \0까지 포함해서 32자이기 때문이다. --- IGNORE ---
        }
        //sscanf는 buffer에서 "INSERT INTO %31s VALUES ( %d , '%31[^']' , %d )" 형식에 맞게 데이터를 읽어오는 함수이다. --- IGNORE ---
        return strcmp(query->table_name, "users") == 0; //여기서 1반환 -> if(!parse_query부분이 0으로 바뀌겠네 -> execute)
    }// 또 query table_name이 users인지 확인하는 이유는 현재 users 테이블만 지원하기 때문이다. --- IGNORE ---

    if (strncmp(buffer, "SELECT ", 7) == 0) {
        query->type = QUERY_SELECT;
        from_pos = strstr(buffer, " FROM ");
        //strstr는 buffer에서 " FROM "이 처음 나오는 위치를 찾는 함수이다. --- IGNORE ---
        //테이블명은 매번 다르기 때문에, " FROM "을 기준으로 앞부분은 컬럼명, 뒷부분은 테이블명으로 나누어서 처리한다. --- IGNORE --- 
        //
        if (from_pos == NULL) {
            return 0;
        }

        *from_pos = '\0';
        strncpy(columns, buffer + 7, sizeof(columns) - 1);
        columns[sizeof(columns) - 1] = '\0';
        strcpy(query->table_name, from_pos + 6);
        if (strcmp(query->table_name, "users") != 0) {
            return 0;
        }

        trim_token(columns);
        if (strcmp(columns, "*") == 0) {
            query->select_all = 1;
            return 1;
        }
        return parse_selected_columns(columns, query);
    }

    return 0;
}
