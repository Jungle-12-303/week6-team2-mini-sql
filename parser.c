#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message) {
    if (error == NULL || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static void skip_spaces(const char **cursor) {
    while (**cursor != '\0' && isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }
}

static void trim_in_place(char *text) {
    char *start = text;
    char *end;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    *end = '\0';
}

static int consume_keyword(const char **cursor, const char *keyword) {
    const char *text = *cursor;
    size_t index = 0;

    while (keyword[index] != '\0') {
        if (text[index] == '\0') {
            return 0;
        }

        if (tolower((unsigned char)text[index]) !=
            tolower((unsigned char)keyword[index])) {
            return 0;
        }

        index++;
    }

    if (text[index] != '\0' && !isspace((unsigned char)text[index])) {
        return 0;
    }

    *cursor = text + index;
    return 1;
}

static int starts_with_keyword(const char *text, const char *keyword) {
    size_t index = 0;

    while (keyword[index] != '\0') {
        if (text[index] == '\0') {
            return 0;
        }

        if (tolower((unsigned char)text[index]) !=
            tolower((unsigned char)keyword[index])) {
            return 0;
        }

        index++;
    }

    return 1;
}

static int parse_identifier(const char **cursor,
                            char *destination,
                            size_t destination_size,
                            const char *identifier_name,
                            char *error,
                            size_t error_size) {
    const char *text = *cursor;
    size_t length = 0;
    char message[MAX_ERROR_LENGTH];

    if (!(isalpha((unsigned char)*text) || *text == '_')) {
        snprintf(message, sizeof(message), "Expected %s", identifier_name);
        set_error(error, error_size, message);
        return 0;
    }

    while (isalnum((unsigned char)*text) || *text == '_') {
        if (length + 1 >= destination_size) {
            snprintf(message,
                     sizeof(message),
                     "%s is too long",
                     identifier_name);
            set_error(error, error_size, message);
            return 0;
        }

        destination[length++] = *text;
        text++;
    }

    destination[length] = '\0';
    *cursor = text;
    return 1;
}

static int parse_select_column_list(const char **cursor,
                                    Statement *stmt,
                                    char *error,
                                    size_t error_size) {
    const char *text = *cursor;

    stmt->select_all = 0;
    stmt->select_column_count = 0;

    for (;;) {
        if (stmt->select_column_count >= MAX_SELECT_COLUMNS) {
            set_error(error, error_size, "Too many selected columns");
            return 0;
        }

        if (!parse_identifier(&text,
                              stmt->select_columns[stmt->select_column_count],
                              MAX_COLUMN_NAME,
                              "column name",
                              error,
                              error_size)) {
            return 0;
        }

        stmt->select_column_count++;
        skip_spaces(&text);

        if (*text == ',') {
            text++;
            skip_spaces(&text);
            continue;
        }

        if (consume_keyword(&text, "FROM")) {
            *cursor = text;
            return 1;
        }

        set_error(error, error_size, "Expected ',' or FROM after column name");
        return 0;
    }
}

static int parse_unquoted_value(const char **cursor,
                                char *destination,
                                size_t destination_size,
                                char *error,
                                size_t error_size) {
    const char *start = *cursor;
    const char *end = start;
    size_t length;
    size_t index;

    while (*end != '\0' && *end != ',' && *end != ')') {
        end++;
    }

    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    while (start < end && isspace((unsigned char)*start)) {
        start++;
    }

    if (start == end) {
        set_error(error, error_size, "Empty value is not allowed");
        return 0;
    }

    length = (size_t)(end - start);
    if (length >= destination_size) {
        set_error(error, error_size, "Value is too long");
        return 0;
    }

    for (index = 0; index < length; index++) {
        if (isspace((unsigned char)start[index])) {
            set_error(error, error_size,
                      "Unquoted values cannot contain spaces");
            return 0;
        }

        destination[index] = start[index];
    }

    destination[length] = '\0';
    *cursor = end;
    return 1;
}

static int parse_quoted_value(const char **cursor,
                              char *destination,
                              size_t destination_size,
                              char *error,
                              size_t error_size) {
    const char *text = *cursor;
    size_t length = 0;

    text++;

    while (*text != '\0' && *text != '\'') {
        if (length + 1 >= destination_size) {
            set_error(error, error_size, "Value is too long");
            return 0;
        }

        destination[length++] = *text;
        text++;
    }

    if (*text != '\'') {
        set_error(error, error_size, "Unterminated string value");
        return 0;
    }

    destination[length] = '\0';
    text++;
    *cursor = text;
    return 1;
}

static int parse_values_list(const char **cursor,
                             Statement *stmt,
                             char *error,
                             size_t error_size) {
    const char *text = *cursor;

    if (*text != '(') {
        set_error(error, error_size, "Expected '(' after VALUES");
        return 0;
    }

    text++;
    stmt->value_count = 0;

    for (;;) {
        skip_spaces(&text);

        if (*text == ')') {
            set_error(error, error_size, "VALUES list cannot be empty");
            return 0;
        }

        if (stmt->value_count >= MAX_VALUES) {
            set_error(error, error_size, "Too many values");
            return 0;
        }

        if (*text == '\'') {
            if (!parse_quoted_value(&text,
                                    stmt->values[stmt->value_count],
                                    MAX_VALUE_LENGTH,
                                    error,
                                    error_size)) {
                return 0;
            }
        } else {
            if (!parse_unquoted_value(&text,
                                      stmt->values[stmt->value_count],
                                      MAX_VALUE_LENGTH,
                                      error,
                                      error_size)) {
                return 0;
            }
        }

        stmt->value_count++;
        skip_spaces(&text);

        if (*text == ',') {
            text++;
            continue;
        }

        if (*text == ')') {
            text++;
            break;
        }

        set_error(error, error_size, "Expected ',' or ')' in VALUES list");
        return 0;
    }

    skip_spaces(&text);
    if (*text != '\0') {
        set_error(error, error_size, "Unexpected text after VALUES list");
        return 0;
    }

    *cursor = text;
    return 1;
}

static int parse_insert(const char *sql,
                        Statement *stmt,
                        char *error,
                        size_t error_size) {
    const char *cursor = sql;

    stmt->type = STATEMENT_INSERT;
    stmt->value_count = 0;
    stmt->select_all = 0;
    stmt->select_column_count = 0;

    if (!consume_keyword(&cursor, "INSERT")) {
        set_error(error, error_size, "Expected INSERT");
        return 0;
    }

    skip_spaces(&cursor);
    if (!consume_keyword(&cursor, "INTO")) {
        set_error(error, error_size, "Expected INTO after INSERT");
        return 0;
    }

    skip_spaces(&cursor);
    if (!parse_identifier(&cursor,
                          stmt->table_name,
                          sizeof(stmt->table_name),
                          "table name",
                          error,
                          error_size)) {
        return 0;
    }

    skip_spaces(&cursor);
    if (!consume_keyword(&cursor, "VALUES")) {
        set_error(error, error_size, "Expected VALUES after table name");
        return 0;
    }

    skip_spaces(&cursor);
    return parse_values_list(&cursor, stmt, error, error_size);
}

static int parse_select(const char *sql,
                        Statement *stmt,
                        char *error,
                        size_t error_size) {
    const char *cursor = sql;
    int from_consumed = 0;

    stmt->type = STATEMENT_SELECT;
    stmt->value_count = 0;
    stmt->select_all = 0;
    stmt->select_column_count = 0;

    if (!consume_keyword(&cursor, "SELECT")) {
        set_error(error, error_size, "Expected SELECT");
        return 0;
    }

    skip_spaces(&cursor);
    if (*cursor == '*') {
        stmt->select_all = 1;
        cursor++;
        skip_spaces(&cursor);
    } else {
        if (!parse_select_column_list(&cursor, stmt, error, error_size)) {
            return 0;
        }

        from_consumed = 1;
    }

    if (!from_consumed && !consume_keyword(&cursor, "FROM")) {
        set_error(error, error_size, "Expected FROM after SELECT *");
        return 0;
    }

    skip_spaces(&cursor);
    if (!parse_identifier(&cursor,
                          stmt->table_name,
                          sizeof(stmt->table_name),
                          "table name",
                          error,
                          error_size)) {
        return 0;
    }

    skip_spaces(&cursor);
    if (*cursor != '\0') {
        set_error(error, error_size, "Unexpected text after table name");
        return 0;
    }

    return 1;
}

int parse_sql(const char *sql, Statement *stmt, char *error, size_t error_size) {
    char buffer[MAX_SQL_LENGTH];
    char *semicolon;
    char *last_semicolon;
    size_t length;

    if (sql == NULL || stmt == NULL) {
        set_error(error, error_size, "Internal parser error");
        return 0;
    }

    length = strlen(sql);
    if (length >= sizeof(buffer)) {
        set_error(error, error_size, "SQL input is too long");
        return 0;
    }

    memcpy(buffer, sql, length + 1);
    trim_in_place(buffer);

    if (buffer[0] == '\0') {
        set_error(error, error_size, "SQL input is empty");
        return 0;
    }

    semicolon = strchr(buffer, ';');
    if (semicolon == NULL) {
        set_error(error, error_size, "SQL statement must end with ';'");
        return 0;
    }

    last_semicolon = strrchr(buffer, ';');
    if (semicolon != last_semicolon) {
        set_error(error, error_size, "Only one SQL statement is supported");
        return 0;
    }

    if (semicolon[1] != '\0') {
        set_error(error, error_size, "Unexpected text after ';'");
        return 0;
    }

    *semicolon = '\0';
    trim_in_place(buffer);

    if (starts_with_keyword(buffer, "INSERT")) {
        return parse_insert(buffer, stmt, error, error_size);
    }

    if (starts_with_keyword(buffer, "SELECT")) {
        return parse_select(buffer, stmt, error, error_size);
    }

    set_error(error, error_size, "Only INSERT and SELECT are supported");
    return 0;
}
