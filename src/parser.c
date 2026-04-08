#include "mini_sql.h"

#include <stdlib.h>
#include <string.h>

typedef struct Parser {
    const TokenList *tokens;
    size_t current;
    char *error_buf;
    size_t error_size;
} Parser;

static const Token *peek(Parser *parser) {
    return &parser->tokens->items[parser->current];
}

static const Token *previous(Parser *parser) {
    return &parser->tokens->items[parser->current - 1U];
}

static bool is_at_end(Parser *parser) {
    return peek(parser)->type == TOKEN_EOF;
}

static const Token *advance(Parser *parser) {
    if (!is_at_end(parser)) {
        parser->current += 1U;
    }
    return previous(parser);
}

static bool check(Parser *parser, TokenType type) {
    if (is_at_end(parser)) {
        return type == TOKEN_EOF;
    }
    return peek(parser)->type == type;
}

static bool match(Parser *parser, TokenType type) {
    if (!check(parser, type)) {
        return false;
    }
    advance(parser);
    return true;
}

static const char *token_name(TokenType type) {
    switch (type) {
        case TOKEN_IDENTIFIER: return "identifier";
        case TOKEN_NUMBER: return "number";
        case TOKEN_STRING: return "string";
        case TOKEN_INSERT: return "INSERT";
        case TOKEN_INTO: return "INTO";
        case TOKEN_VALUES: return "VALUES";
        case TOKEN_SELECT: return "SELECT";
        case TOKEN_FROM: return "FROM";
        case TOKEN_WHERE: return "WHERE";
        case TOKEN_STAR: return "*";
        case TOKEN_COMMA: return ",";
        case TOKEN_LPAREN: return "(";
        case TOKEN_RPAREN: return ")";
        case TOKEN_SEMICOLON: return ";";
        case TOKEN_EQUALS: return "=";
        case TOKEN_DOT: return ".";
        case TOKEN_EOF: return "end of file";
    }
    return "token";
}

static bool parse_error(Parser *parser, const char *message) {
    const Token *token = peek(parser);

    set_error(parser->error_buf, parser->error_size, "%s at %d:%d near %s", message, token->line, token->column,
              token_name(token->type));
    return false;
}

static bool ensure_string_capacity(char ***items, size_t *count, size_t *capacity, char *error_buf, size_t error_size) {
    char **new_items;
    size_t new_capacity;

    if (*count < *capacity) {
        return true;
    }

    new_capacity = *capacity == 0U ? 8U : (*capacity * 2U);
    new_items = realloc(*items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) {
        set_error(error_buf, error_size, "out of memory while parsing");
        return false;
    }

    *items = new_items;
    *capacity = new_capacity;
    return true;
}

static bool append_string(char ***items, size_t *count, size_t *capacity, const char *text,
                          char *error_buf, size_t error_size) {
    char *copy;

    if (!ensure_string_capacity(items, count, capacity, error_buf, error_size)) {
        return false;
    }

    copy = msql_strdup(text);
    if (copy == NULL) {
        set_error(error_buf, error_size, "out of memory while parsing");
        return false;
    }

    (*items)[*count] = copy;
    *count += 1U;
    return true;
}

static bool ensure_statement_capacity(StatementList *statements, char *error_buf, size_t error_size) {
    Statement *new_items;
    size_t new_capacity;

    if (statements->count < statements->capacity) {
        return true;
    }

    new_capacity = statements->capacity == 0U ? 8U : statements->capacity * 2U;
    new_items = realloc(statements->items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) {
        set_error(error_buf, error_size, "out of memory while parsing");
        return false;
    }

    statements->items = new_items;
    statements->capacity = new_capacity;
    return true;
}

static bool append_statement(StatementList *statements, Statement statement, char *error_buf, size_t error_size) {
    if (!ensure_statement_capacity(statements, error_buf, error_size)) {
        return false;
    }

    statements->items[statements->count] = statement;
    statements->count += 1U;
    return true;
}

static bool consume(Parser *parser, TokenType type, const char *message) {
    if (check(parser, type)) {
        advance(parser);
        return true;
    }
    return parse_error(parser, message);
}

static bool parse_identifier(Parser *parser, char **out_name) {
    const Token *token;

    if (!consume(parser, TOKEN_IDENTIFIER, "expected identifier")) {
        return false;
    }

    token = previous(parser);
    *out_name = msql_strdup(token->text);
    if (*out_name == NULL) {
        set_error(parser->error_buf, parser->error_size, "out of memory while parsing");
        return false;
    }

    return true;
}

static bool parse_qualified_name(Parser *parser, char **out_name) {
    char *name = NULL;

    if (!parse_identifier(parser, &name)) {
        return false;
    }

    while (match(parser, TOKEN_DOT)) {
        char *part = NULL;
        char *combined;
        size_t combined_length;

        if (!parse_identifier(parser, &part)) {
            free(name);
            return false;
        }

        combined_length = strlen(name) + 1U + strlen(part);
        combined = malloc(combined_length + 1U);
        if (combined == NULL) {
            free(name);
            free(part);
            set_error(parser->error_buf, parser->error_size, "out of memory while parsing");
            return false;
        }

        snprintf(combined, combined_length + 1U, "%s.%s", name, part);
        free(name);
        free(part);
        name = combined;
    }

    *out_name = name;
    return true;
}

static bool parse_value(Parser *parser, char **out_value) {
    const Token *token = peek(parser);

    if (token->type != TOKEN_STRING && token->type != TOKEN_NUMBER && token->type != TOKEN_IDENTIFIER) {
        return parse_error(parser, "expected literal value");
    }

    *out_value = msql_strdup(token->text);
    if (*out_value == NULL) {
        set_error(parser->error_buf, parser->error_size, "out of memory while parsing");
        return false;
    }

    advance(parser);
    return true;
}

static bool parse_identifier_list(Parser *parser, char ***out_items, size_t *out_count) {
    char **items = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    char *name = NULL;

    if (!parse_identifier(parser, &name)) {
        return false;
    }

    if (!append_string(&items, &count, &capacity, name, parser->error_buf, parser->error_size)) {
        free(name);
        return false;
    }
    free(name);

    while (match(parser, TOKEN_COMMA)) {
        if (!parse_identifier(parser, &name)) {
            free_string_array(items, count);
            return false;
        }
        if (!append_string(&items, &count, &capacity, name, parser->error_buf, parser->error_size)) {
            free(name);
            free_string_array(items, count);
            return false;
        }
        free(name);
    }

    *out_items = items;
    *out_count = count;
    return true;
}

static bool parse_value_list(Parser *parser, char ***out_items, size_t *out_count) {
    char **items = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    char *value = NULL;

    if (!parse_value(parser, &value)) {
        return false;
    }

    if (!append_string(&items, &count, &capacity, value, parser->error_buf, parser->error_size)) {
        free(value);
        return false;
    }
    free(value);

    while (match(parser, TOKEN_COMMA)) {
        if (!parse_value(parser, &value)) {
            free_string_array(items, count);
            return false;
        }
        if (!append_string(&items, &count, &capacity, value, parser->error_buf, parser->error_size)) {
            free(value);
            free_string_array(items, count);
            return false;
        }
        free(value);
    }

    *out_items = items;
    *out_count = count;
    return true;
}

static bool parse_insert_statement(Parser *parser, Statement *out_statement) {
    Statement statement;

    memset(&statement, 0, sizeof(statement));
    statement.type = STATEMENT_INSERT;

    if (!consume(parser, TOKEN_INSERT, "expected INSERT")) {
        return false;
    }
    if (!consume(parser, TOKEN_INTO, "expected INTO after INSERT")) {
        return false;
    }
    if (!parse_qualified_name(parser, &statement.as.insert_stmt.table_name)) {
        return false;
    }

    if (match(parser, TOKEN_LPAREN)) {
        if (!parse_identifier_list(parser, &statement.as.insert_stmt.columns, &statement.as.insert_stmt.column_count)) {
            free(statement.as.insert_stmt.table_name);
            return false;
        }
        if (!consume(parser, TOKEN_RPAREN, "expected ')' after column list")) {
            free(statement.as.insert_stmt.table_name);
            free_string_array(statement.as.insert_stmt.columns, statement.as.insert_stmt.column_count);
            return false;
        }
    }

    if (!consume(parser, TOKEN_VALUES, "expected VALUES")) {
        free(statement.as.insert_stmt.table_name);
        free_string_array(statement.as.insert_stmt.columns, statement.as.insert_stmt.column_count);
        return false;
    }
    if (!consume(parser, TOKEN_LPAREN, "expected '(' after VALUES")) {
        free(statement.as.insert_stmt.table_name);
        free_string_array(statement.as.insert_stmt.columns, statement.as.insert_stmt.column_count);
        return false;
    }
    if (!parse_value_list(parser, &statement.as.insert_stmt.values, &statement.as.insert_stmt.value_count)) {
        free(statement.as.insert_stmt.table_name);
        free_string_array(statement.as.insert_stmt.columns, statement.as.insert_stmt.column_count);
        return false;
    }
    if (!consume(parser, TOKEN_RPAREN, "expected ')' after VALUES list")) {
        free(statement.as.insert_stmt.table_name);
        free_string_array(statement.as.insert_stmt.columns, statement.as.insert_stmt.column_count);
        free_string_array(statement.as.insert_stmt.values, statement.as.insert_stmt.value_count);
        return false;
    }

    if (statement.as.insert_stmt.column_count > 0U &&
        statement.as.insert_stmt.column_count != statement.as.insert_stmt.value_count) {
        free(statement.as.insert_stmt.table_name);
        free_string_array(statement.as.insert_stmt.columns, statement.as.insert_stmt.column_count);
        free_string_array(statement.as.insert_stmt.values, statement.as.insert_stmt.value_count);
        set_error(parser->error_buf, parser->error_size, "column count and value count must match");
        return false;
    }

    *out_statement = statement;
    return true;
}

static bool parse_select_statement(Parser *parser, Statement *out_statement) {
    Statement statement;

    memset(&statement, 0, sizeof(statement));
    statement.type = STATEMENT_SELECT;

    if (!consume(parser, TOKEN_SELECT, "expected SELECT")) {
        return false;
    }

    if (match(parser, TOKEN_STAR)) {
        statement.as.select_stmt.select_all = true;
    } else if (!parse_identifier_list(parser, &statement.as.select_stmt.columns, &statement.as.select_stmt.column_count)) {
        return false;
    }

    if (!consume(parser, TOKEN_FROM, "expected FROM after SELECT list")) {
        free_string_array(statement.as.select_stmt.columns, statement.as.select_stmt.column_count);
        return false;
    }
    if (!parse_qualified_name(parser, &statement.as.select_stmt.table_name)) {
        free_string_array(statement.as.select_stmt.columns, statement.as.select_stmt.column_count);
        return false;
    }

    if (match(parser, TOKEN_WHERE)) {
        statement.as.select_stmt.has_where = true;
        if (!parse_identifier(parser, &statement.as.select_stmt.where_column)) {
            free_string_array(statement.as.select_stmt.columns, statement.as.select_stmt.column_count);
            free(statement.as.select_stmt.table_name);
            return false;
        }
        if (!consume(parser, TOKEN_EQUALS, "expected '=' in WHERE clause")) {
            free_string_array(statement.as.select_stmt.columns, statement.as.select_stmt.column_count);
            free(statement.as.select_stmt.table_name);
            free(statement.as.select_stmt.where_column);
            return false;
        }
        if (!parse_value(parser, &statement.as.select_stmt.where_value)) {
            free_string_array(statement.as.select_stmt.columns, statement.as.select_stmt.column_count);
            free(statement.as.select_stmt.table_name);
            free(statement.as.select_stmt.where_column);
            return false;
        }
    }

    *out_statement = statement;
    return true;
}

bool parse_tokens(const TokenList *tokens, StatementList *out_statements, char *error_buf, size_t error_size) {
    Parser parser;

    parser.tokens = tokens;
    parser.current = 0U;
    parser.error_buf = error_buf;
    parser.error_size = error_size;

    out_statements->items = NULL;
    out_statements->count = 0U;
    out_statements->capacity = 0U;

    while (!is_at_end(&parser)) {
        Statement statement;

        memset(&statement, 0, sizeof(statement));

        while (match(&parser, TOKEN_SEMICOLON)) {
        }

        if (is_at_end(&parser)) {
            break;
        }

        if (check(&parser, TOKEN_INSERT)) {
            if (!parse_insert_statement(&parser, &statement)) {
                free_statement_list(out_statements);
                return false;
            }
        } else if (check(&parser, TOKEN_SELECT)) {
            if (!parse_select_statement(&parser, &statement)) {
                free_statement_list(out_statements);
                return false;
            }
        } else {
            parse_error(&parser, "expected INSERT or SELECT");
            free_statement_list(out_statements);
            return false;
        }

        if (!append_statement(out_statements, statement, error_buf, error_size)) {
            free_statement_list(out_statements);
            return false;
        }

        if (match(&parser, TOKEN_SEMICOLON)) {
            while (match(&parser, TOKEN_SEMICOLON)) {
            }
            continue;
        }

        if (!is_at_end(&parser)) {
            free_statement_list(out_statements);
            return parse_error(&parser, "expected ';' after statement");
        }
    }

    return true;
}

void free_statement_list(StatementList *statements) {
    size_t i;

    if (statements == NULL) {
        return;
    }

    for (i = 0; i < statements->count; ++i) {
        Statement *statement = &statements->items[i];

        if (statement->type == STATEMENT_INSERT) {
            free(statement->as.insert_stmt.table_name);
            free_string_array(statement->as.insert_stmt.columns, statement->as.insert_stmt.column_count);
            free_string_array(statement->as.insert_stmt.values, statement->as.insert_stmt.value_count);
        } else if (statement->type == STATEMENT_SELECT) {
            free_string_array(statement->as.select_stmt.columns, statement->as.select_stmt.column_count);
            free(statement->as.select_stmt.table_name);
            free(statement->as.select_stmt.where_column);
            free(statement->as.select_stmt.where_value);
        }
    }

    free(statements->items);
    statements->items = NULL;
    statements->count = 0U;
    statements->capacity = 0U;
}

