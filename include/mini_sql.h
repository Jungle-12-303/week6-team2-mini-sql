#ifndef MINI_SQL_H
#define MINI_SQL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define MSQL_ERROR_SIZE 512

typedef enum TokenType {
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_INSERT,
    TOKEN_INTO,
    TOKEN_VALUES,
    TOKEN_SELECT,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_STAR,
    TOKEN_COMMA,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_SEMICOLON,
    TOKEN_EQUALS,
    TOKEN_DOT,
    TOKEN_EOF
} TokenType;

typedef struct Token {
    TokenType type;
    char *text;
    int line;
    int column;
} Token;

typedef struct TokenList {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

typedef enum StatementType {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct InsertStatement {
    char *table_name;
    char **columns;
    size_t column_count;
    char **values;
    size_t value_count;
} InsertStatement;

typedef struct SelectStatement {
    bool select_all;
    char **columns;
    size_t column_count;
    char *table_name;
    bool has_where;
    char *where_column;
    char *where_value;
} SelectStatement;

typedef struct Statement {
    StatementType type;
    union {
        InsertStatement insert_stmt;
        SelectStatement select_stmt;
    } as;
} Statement;

typedef struct StatementList {
    Statement *items;
    size_t count;
    size_t capacity;
} StatementList;

typedef struct ExecutionContext {
    const char *db_path;
    FILE *output;
} ExecutionContext;

bool tokenize_sql(const char *input, TokenList *out_tokens, char *error_buf, size_t error_size);
void free_token_list(TokenList *tokens);

bool parse_tokens(const TokenList *tokens, StatementList *out_statements, char *error_buf, size_t error_size);
void free_statement_list(StatementList *statements);

bool execute_statements(const StatementList *statements, const ExecutionContext *context, char *error_buf, size_t error_size);
bool process_sql(const char *sql, const ExecutionContext *context, char *error_buf, size_t error_size);

bool execute_insert_statement(const InsertStatement *statement, const ExecutionContext *context, char *error_buf, size_t error_size);
bool execute_select_statement(const SelectStatement *statement, const ExecutionContext *context, char *error_buf, size_t error_size);

char *msql_strdup(const char *text);
void set_error(char *error_buf, size_t error_size, const char *fmt, ...);
bool read_file_all(const char *path, char **out_contents, char *error_buf, size_t error_size);
void free_string_array(char **items, size_t count);
int find_column_index(char **columns, size_t column_count, const char *target);
bool strings_equal_ci(const char *left, const char *right);

#endif

