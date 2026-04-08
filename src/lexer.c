#include "mini_sql.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void free_token_fields(TokenList *tokens) {
    size_t i;

    for (i = 0; i < tokens->count; ++i) {
        free(tokens->items[i].text);
    }
    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0U;
    tokens->capacity = 0U;
}

static bool ensure_token_capacity(TokenList *tokens, char *error_buf, size_t error_size) {
    Token *new_items;
    size_t new_capacity;

    if (tokens->count < tokens->capacity) {
        return true;
    }

    new_capacity = tokens->capacity == 0U ? 16U : tokens->capacity * 2U;
    new_items = realloc(tokens->items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) {
        set_error(error_buf, error_size, "out of memory while tokenizing");
        return false;
    }

    tokens->items = new_items;
    tokens->capacity = new_capacity;
    return true;
}

static bool push_token_owned(TokenList *tokens, TokenType type, char *text, int line, int column,
                             char *error_buf, size_t error_size) {
    if (!ensure_token_capacity(tokens, error_buf, error_size)) {
        free(text);
        return false;
    }

    tokens->items[tokens->count].type = type;
    tokens->items[tokens->count].text = text;
    tokens->items[tokens->count].line = line;
    tokens->items[tokens->count].column = column;
    tokens->count += 1U;
    return true;
}

static bool push_token_copy(TokenList *tokens, TokenType type, const char *start, size_t length, int line,
                            int column, char *error_buf, size_t error_size) {
    char *text;

    text = malloc(length + 1U);
    if (text == NULL) {
        set_error(error_buf, error_size, "out of memory while tokenizing");
        return false;
    }

    memcpy(text, start, length);
    text[length] = '\0';
    return push_token_owned(tokens, type, text, line, column, error_buf, error_size);
}

static TokenType keyword_type(const char *text) {
    if (strings_equal_ci(text, "INSERT")) {
        return TOKEN_INSERT;
    }
    if (strings_equal_ci(text, "INTO")) {
        return TOKEN_INTO;
    }
    if (strings_equal_ci(text, "VALUES")) {
        return TOKEN_VALUES;
    }
    if (strings_equal_ci(text, "SELECT")) {
        return TOKEN_SELECT;
    }
    if (strings_equal_ci(text, "FROM")) {
        return TOKEN_FROM;
    }
    if (strings_equal_ci(text, "WHERE")) {
        return TOKEN_WHERE;
    }
    return TOKEN_IDENTIFIER;
}

static bool scan_string_literal(const char **cursor, int *line, int *column, TokenList *tokens,
                                char *error_buf, size_t error_size) {
    const char *start = *cursor;
    int start_line = *line;
    int start_column = *column;
    size_t capacity = 32U;
    size_t length = 0U;
    char *buffer = malloc(capacity);

    if (buffer == NULL) {
        set_error(error_buf, error_size, "out of memory while scanning string literal");
        return false;
    }

    *cursor += 1;
    *column += 1;

    while (**cursor != '\0') {
        char current = **cursor;

        if (current == '\'') {
            if ((*cursor)[1] == '\'') {
                if (length + 1U >= capacity) {
                    char *new_buffer;

                    capacity *= 2U;
                    new_buffer = realloc(buffer, capacity);
                    if (new_buffer == NULL) {
                        free(buffer);
                        set_error(error_buf, error_size, "out of memory while scanning string literal");
                        return false;
                    }
                    buffer = new_buffer;
                }

                buffer[length++] = '\'';
                *cursor += 2;
                *column += 2;
                continue;
            }

            *cursor += 1;
            *column += 1;
            buffer[length] = '\0';
            return push_token_owned(tokens, TOKEN_STRING, buffer, start_line, start_column, error_buf, error_size);
        }

        if (length + 1U >= capacity) {
            char *new_buffer;

            capacity *= 2U;
            new_buffer = realloc(buffer, capacity);
            if (new_buffer == NULL) {
                free(buffer);
                set_error(error_buf, error_size, "out of memory while scanning string literal");
                return false;
            }
            buffer = new_buffer;
        }

        buffer[length++] = current;
        *cursor += 1;
        if (current == '\n') {
            *line += 1;
            *column = 1;
        } else {
            *column += 1;
        }
    }

    free(buffer);
    set_error(error_buf, error_size, "unterminated string literal at %d:%d", start_line, start_column);
    (void) start;
    return false;
}

bool tokenize_sql(const char *input, TokenList *out_tokens, char *error_buf, size_t error_size) {
    const char *cursor = input;
    int line = 1;
    int column = 1;

    out_tokens->items = NULL;
    out_tokens->count = 0U;
    out_tokens->capacity = 0U;

    while (*cursor != '\0') {
        char current = *cursor;
        int token_line = line;
        int token_column = column;

        if (current == ' ' || current == '\t' || current == '\r') {
            cursor += 1;
            column += 1;
            continue;
        }

        if (current == '\n') {
            cursor += 1;
            line += 1;
            column = 1;
            continue;
        }

        if (current == '-' && cursor[1] == '-') {
            cursor += 2;
            column += 2;
            while (*cursor != '\0' && *cursor != '\n') {
                cursor += 1;
                column += 1;
            }
            continue;
        }

        if (isalpha((unsigned char) current) || current == '_') {
            const char *start = cursor;
            size_t length;
            char *identifier_text;
            TokenType type;

            while (isalnum((unsigned char) *cursor) || *cursor == '_') {
                cursor += 1;
                column += 1;
            }

            length = (size_t) (cursor - start);
            identifier_text = malloc(length + 1U);
            if (identifier_text == NULL) {
                set_error(error_buf, error_size, "out of memory while tokenizing");
                free_token_fields(out_tokens);
                return false;
            }
            memcpy(identifier_text, start, length);
            identifier_text[length] = '\0';
            type = keyword_type(identifier_text);

            if (!push_token_owned(out_tokens, type, identifier_text, token_line, token_column, error_buf, error_size)) {
                free_token_fields(out_tokens);
                return false;
            }
            continue;
        }

        if (isdigit((unsigned char) current)) {
            const char *start = cursor;

            while (isdigit((unsigned char) *cursor)) {
                cursor += 1;
                column += 1;
            }

            if (*cursor == '.' && isdigit((unsigned char) cursor[1])) {
                cursor += 1;
                column += 1;
                while (isdigit((unsigned char) *cursor)) {
                    cursor += 1;
                    column += 1;
                }
            }

            if (!push_token_copy(out_tokens, TOKEN_NUMBER, start, (size_t) (cursor - start), token_line, token_column,
                                 error_buf, error_size)) {
                free_token_fields(out_tokens);
                return false;
            }
            continue;
        }

        if (current == '\'') {
            if (!scan_string_literal(&cursor, &line, &column, out_tokens, error_buf, error_size)) {
                free_token_fields(out_tokens);
                return false;
            }
            continue;
        }

        switch (current) {
            case '*':
                if (!push_token_copy(out_tokens, TOKEN_STAR, cursor, 1U, token_line, token_column, error_buf, error_size)) {
                    free_token_fields(out_tokens);
                    return false;
                }
                break;
            case ',':
                if (!push_token_copy(out_tokens, TOKEN_COMMA, cursor, 1U, token_line, token_column, error_buf, error_size)) {
                    free_token_fields(out_tokens);
                    return false;
                }
                break;
            case '(':
                if (!push_token_copy(out_tokens, TOKEN_LPAREN, cursor, 1U, token_line, token_column, error_buf, error_size)) {
                    free_token_fields(out_tokens);
                    return false;
                }
                break;
            case ')':
                if (!push_token_copy(out_tokens, TOKEN_RPAREN, cursor, 1U, token_line, token_column, error_buf, error_size)) {
                    free_token_fields(out_tokens);
                    return false;
                }
                break;
            case ';':
                if (!push_token_copy(out_tokens, TOKEN_SEMICOLON, cursor, 1U, token_line, token_column, error_buf, error_size)) {
                    free_token_fields(out_tokens);
                    return false;
                }
                break;
            case '=':
                if (!push_token_copy(out_tokens, TOKEN_EQUALS, cursor, 1U, token_line, token_column, error_buf, error_size)) {
                    free_token_fields(out_tokens);
                    return false;
                }
                break;
            case '.':
                if (!push_token_copy(out_tokens, TOKEN_DOT, cursor, 1U, token_line, token_column, error_buf, error_size)) {
                    free_token_fields(out_tokens);
                    return false;
                }
                break;
            default:
                set_error(error_buf, error_size, "unexpected character '%c' at %d:%d", current, token_line, token_column);
                free_token_fields(out_tokens);
                return false;
        }

        cursor += 1;
        column += 1;
    }

    if (!push_token_copy(out_tokens, TOKEN_EOF, "", 0U, line, column, error_buf, error_size)) {
        free_token_fields(out_tokens);
        return false;
    }

    return true;
}

void free_token_list(TokenList *tokens) {
    if (tokens == NULL) {
        return;
    }

    free_token_fields(tokens);
}

