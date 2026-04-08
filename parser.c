#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

/* This trims SQL so parsing can assume one clean line. */
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

/* This trims one token so strcmp can compare exact names. */
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

/* This parses the selected column list into the Query struct. */
static int parse_selected_columns(char *columns, Query *query) {
    char *token;

    query->selected_column_count = 0;
    token = strtok(columns, ",");
    while (token != NULL) {
        if (query->selected_column_count >= 3) {
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

/* This parses INSERT because executor needs structured values, not raw SQL. */
static int parse_insert(char *sql, Query *query) {
    query->type = QUERY_INSERT;

    /* 1. Match the fixed INSERT form */
    if (sscanf(sql, "INSERT INTO %31s VALUES ( %d , '%31[^']' , %d )",
                query->table_name, &query->id, query->name, &query->age) != 4) {
        return 0;
    }

    /* 2. Only users is supported in this project */
    return strcmp(query->table_name, "users") == 0;
}

/* This parses SELECT because the executor only needs table and columns. */
static int parse_select(char *sql, Query *query) {
    char *from_pos;
    char columns[96];

    query->type = QUERY_SELECT;

    /* 1. Split columns and table name */
    from_pos = strstr(sql, " FROM ");
    if (from_pos == NULL) {
        return 0;
    }
    *from_pos = '\0';
    strncpy(columns, sql + 7, sizeof(columns) - 1);
    columns[sizeof(columns) - 1] = '\0';
    strncpy(query->table_name, from_pos + 6, sizeof(query->table_name) - 1);
    query->table_name[sizeof(query->table_name) - 1] = '\0';
    trim_token(query->table_name);

    /* 2. Only users is supported in this project */
    if (strcmp(query->table_name, "users") != 0) {
        return 0;
    }

    /* 3. Parse either * or a small column list */
    trim_token(columns);
    if (strcmp(columns, "*") == 0) {
        query->select_all = 1;
        return 1;
    }
    return parse_selected_columns(columns, query);
}

/* This turns one SQL string into the Query shape used by later layers. */
int parse_query(const char *sql, Query *query) {
    char buffer[256];

    memset(query, 0, sizeof(Query));
    strncpy(buffer, sql, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    /* 1. Normalize the input line */
    trim_sql(buffer);

    /* 2. Decide which SQL type it is */
    if (strncmp(buffer, "INSERT INTO ", 12) == 0) {
        return parse_insert(buffer, query);
    }
    if (strncmp(buffer, "SELECT ", 7) == 0) {
        return parse_select(buffer, query);
    }

    /* 3. Reject anything outside this MVP */
    return 0;
}
