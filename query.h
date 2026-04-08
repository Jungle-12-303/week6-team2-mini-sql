#ifndef QUERY_H
#define QUERY_H

typedef enum {
    QUERY_INSERT,
    QUERY_SELECT
} QueryType;

typedef struct {
    QueryType type;
    char table_name[32];
    int id;
    char name[32];
    int age;
    int select_all;
    char selected_columns[3][32];
    int selected_column_count;
} Query;

#endif
