#include "mini_sql.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/*
 * lexer 는 SQL 원문 문자열을 읽어서 TokenList 로 바꾼다.
 * parser 는 이 TokenList 만 보고 문장을 해석하므로,
 * "어떤 글자 조합을 어떤 토큰으로 볼지"는 전부 이 파일에서 결정된다.
 */

/* TokenList 내부에 저장된 token 문자열과 배열 메모리를 함께 정리한다. */
static void free_token_fields(TokenList *tokens) {
    /* 토큰 배열을 순회하기 위한 인덱스다. */
    size_t i;

    /* 각 토큰이 소유한 text 문자열을 하나씩 해제한다. */
    for (i = 0; i < tokens->count; ++i) {
        free(tokens->items[i].text);
    }
    /* 토큰 구조체 배열 자체도 해제한다. */
    free(tokens->items);
    /* 정리 후 재사용 실수를 막기 위해 포인터를 비운다. */
    tokens->items    = NULL;
    /* 현재 토큰 개수를 0으로 되돌린다. */
    tokens->count    = 0U;
    /* 확보했던 용량도 0으로 되돌린다. */
    tokens->capacity = 0U;
}

/* 새 토큰이 들어갈 자리가 부족하면 토큰 배열 크기를 늘린다. */
static bool ensure_token_capacity(TokenList *tokens, ErrorContext *err) {
    /* 재할당 후 새 배열 포인터를 담을 변수다. */
    Token *new_items;
    /* 새 배열 용량이다. */
    size_t new_capacity;

    /* 아직 빈 자리가 있으면 그대로 사용하면 된다. */
    if (tokens->count < tokens->capacity) {
        return true;
    }

    /* 비어 있으면 16칸으로 시작하고, 이후에는 2배씩 늘린다. */
    new_capacity = tokens->capacity == 0U ? 16U : tokens->capacity * 2U;
    /* 더 큰 토큰 배열을 확보한다. */
    new_items = realloc(tokens->items, new_capacity * sizeof(*new_items));
    /* 재할당에 실패하면 토큰화를 더 진행할 수 없다. */
    if (new_items == NULL) {
        set_error(err, "SQL 토큰화 중 메모리가 부족합니다");
        return false;
    }

    /* 새 배열 포인터를 저장한다. */
    tokens->items    = new_items;
    /* 새 용량을 저장한다. */
    tokens->capacity = new_capacity;
    /* 용량 확보 성공을 반환한다. */
    return true;
}

/* 이미 소유권을 가진 문자열을 그대로 token text 로 저장한다. */
static bool push_token_owned(TokenList *tokens, TokenType type, char *text, int line, int column,
                             ErrorContext *err) {
    /* 새 토큰이 들어갈 자리가 있는지 먼저 확인한다. */
    if (!ensure_token_capacity(tokens, err)) {
        /* 토큰 저장에 실패했으므로 전달받은 문자열도 여기서 정리한다. */
        free(text);
        return false;
    }

    /* 새 토큰의 종류를 기록한다. */
    tokens->items[tokens->count].type   = type;
    /* 새 토큰이 소유할 문자열 포인터를 기록한다. */
    tokens->items[tokens->count].text   = text;
    /* 토큰이 시작된 줄 번호를 기록한다. */
    tokens->items[tokens->count].line   = line;
    /* 토큰이 시작된 열 번호를 기록한다. */
    tokens->items[tokens->count].column = column;
    /* 토큰 개수를 하나 늘린다. */
    tokens->count += 1U;
    /* 토큰 추가 성공을 반환한다. */
    return true;
}

/* 원문 일부를 복사해 새 token text 를 만든 뒤 저장한다. */
static bool push_token_copy(TokenList *tokens, TokenType type, const char *start, size_t length, int line,
                            int column, ErrorContext *err) {
    /* 원문 일부를 복사해 담을 새 문자열 버퍼다. */
    char *text;

    /* 길이 + 널 종료 문자를 담을 만큼 메모리를 잡는다. */
    text = malloc(length + 1U);
    /* 할당에 실패하면 더 진행할 수 없다. */
    if (text == NULL) {
        set_error(err, "SQL 토큰화 중 메모리가 부족합니다");
        return false;
    }

    /* 지정한 길이만큼 원문을 복사한다. */
    memcpy(text, start, length);
    /* C 문자열이 되도록 끝에 널 종료 문자를 붙인다. */
    text[length] = '\0';
    /* 복사한 문자열의 소유권을 토큰 배열로 넘긴다. */
    return push_token_owned(tokens, type, text, line, column, err);
}

/* 식별자처럼 읽은 문자열이 예약어인지 일반 이름인지 판별한다. */
static TokenType keyword_type(const char *text) {
    if (strings_equal_ci(text, "INSERT"))  return TOKEN_INSERT;
    if (strings_equal_ci(text, "INTO"))    return TOKEN_INTO;
    if (strings_equal_ci(text, "VALUES"))  return TOKEN_VALUES;
    if (strings_equal_ci(text, "SELECT"))  return TOKEN_SELECT;
    if (strings_equal_ci(text, "TOP"))     return TOKEN_TOP;
    if (strings_equal_ci(text, "FROM"))    return TOKEN_FROM;
    if (strings_equal_ci(text, "WHERE"))   return TOKEN_WHERE;
    if (strings_equal_ci(text, "ORDER"))   return TOKEN_ORDER;
    if (strings_equal_ci(text, "BY"))      return TOKEN_BY;
    if (strings_equal_ci(text, "ASC"))     return TOKEN_ASC;
    if (strings_equal_ci(text, "DESC"))    return TOKEN_DESC;
    if (strings_equal_ci(text, "LIMIT"))   return TOKEN_LIMIT;
    if (strings_equal_ci(text, "PRIMARY")) return TOKEN_PRIMARY;
    if (strings_equal_ci(text, "KEY"))     return TOKEN_KEY;
    if (strings_equal_ci(text, "CREATE"))  return TOKEN_CREATE;
    if (strings_equal_ci(text, "TABLE"))   return TOKEN_TABLE;
    if (strings_equal_ci(text, "DROP"))    return TOKEN_DROP;
    if (strings_equal_ci(text, "DELETE"))  return TOKEN_DELETE;
    return TOKEN_IDENTIFIER;
}

/*
 * 문자열 버퍼 크기가 부족할 때 2배로 늘린다.
 * 실패 시 buf 를 free 하고 false 를 반환하므로 호출 후 buf 를 다시 사용하면 안 된다.
 */
static bool grow_string_buf(char **buf, size_t *capacity, ErrorContext *err) {
    /* 기존 버퍼보다 2배 큰 버퍼를 다시 할당한다. */
    char *new_buf = realloc(*buf, *capacity * 2U);
    /* 재할당 실패 시 현재 버퍼를 더 이상 유지하지 않고 정리한다. */
    if (new_buf == NULL) {
        free(*buf);
        *buf = NULL;
        set_error(err, "문자열 리터럴을 읽는 중 메모리가 부족합니다");
        return false;
    }
    /* 새 버퍼 포인터를 기록한다. */
    *buf      = new_buf;
    /* 현재 용량도 2배로 갱신한다. */
    *capacity *= 2U;
    /* 버퍼 확장 성공을 반환한다. */
    return true;
}

/*
 * 작은따옴표 문자열을 스캔한다.
 * SQL 스타일 이스케이프인 '' 도 지원해서 실제 값에는 ' 하나만 남긴다.
 */
static bool scan_string_literal(const char **cursor, int *line, int *column, TokenList *tokens,
                                ErrorContext *err) {
    /* 문자열 리터럴이 시작된 줄 번호다. */
    int start_line   = *line;
    /* 문자열 리터럴이 시작된 열 번호다. */
    int start_column = *column;
    /* 문자열 버퍼 초기 용량이다. */
    size_t capacity  = 32U;
    /* 현재까지 읽은 문자열 길이다. */
    size_t length    = 0U;
    /* 문자열 리터럴 내용을 모아 둘 버퍼다. */
    char *buffer     = malloc(capacity);

    /* 초기 버퍼 할당에 실패하면 토큰화를 더 진행할 수 없다. */
    if (buffer == NULL) {
        set_error(err, "문자열 리터럴을 읽는 중 메모리가 부족합니다");
        return false;
    }

    /* 여는 작은따옴표는 소비하고 본문으로 들어간다. */
    *cursor += 1;
    /* 열 위치도 한 칸 진행한다. */
    *column += 1;

    /* 닫는 따옴표를 만날 때까지 한 글자씩 읽는다. */
    while (**cursor != '\0') {
        /* 현재 읽고 있는 문자다. */
        char current = **cursor;

        /* 작은따옴표를 만나면 문자열 종료 또는 SQL 이스케이프인지 본다. */
        if (current == '\'') {
            /* '' 는 이스케이프된 작은따옴표 하나다. */
            if ((*cursor)[1] == '\'') {
                /* 버퍼가 꽉 찼으면 먼저 늘린다. */
                if (length + 1U >= capacity && !grow_string_buf(&buffer, &capacity, err)) {
                    return false;
                }
                /* 실제 문자열 값에는 작은따옴표 하나만 넣는다. */
                buffer[length++] = '\'';
                /* 입력 커서를 두 칸 전진시킨다. */
                *cursor += 2;
                /* 열 위치도 두 칸 전진한다. */
                *column += 2;
                continue;
            }
            /* 닫는 따옴표 */
            /* 닫는 따옴표를 소비한다. */
            *cursor += 1;
            /* 열 위치도 한 칸 전진한다. */
            *column += 1;
            /* 문자열 끝에 널 종료 문자를 붙인다. */
            buffer[length] = '\0';
            /* 완성된 문자열을 STRING 토큰으로 저장한다. */
            return push_token_owned(tokens, TOKEN_STRING, buffer, start_line, start_column, err);
        }

        /* 일반 문자를 추가하기 전 버퍼 용량을 확인한다. */
        if (length + 1U >= capacity && !grow_string_buf(&buffer, &capacity, err)) {
            return false;
        }

        /* 현재 문자를 문자열 버퍼 뒤에 추가한다. */
        buffer[length++] = current;
        /* 입력 커서를 한 글자 전진한다. */
        *cursor += 1;
        /* 줄바꿈이면 줄/열 정보를 새 줄 기준으로 갱신한다. */
        if (current == '\n') {
            *line   += 1;
            *column  = 1;
        } else {
            /* 일반 문자는 열 위치만 한 칸 늘린다. */
            *column += 1;
        }
    }

    /* 입력 끝까지 갔는데 닫는 따옴표를 못 찾았으므로 버퍼를 정리한다. */
    free(buffer);
    /* 사람이 이해하기 쉬운 위치 정보와 함께 오류를 남긴다. */
    set_error(err, "%d:%d 위치의 문자열 리터럴이 닫히지 않았습니다", start_line, start_column);
    /* 문자열 스캔 실패를 반환한다. */
    return false;
}

/*
 * SQL 문자열 전체를 처음부터 끝까지 한 글자씩 읽으며 토큰화한다.
 *
 * 큰 흐름은 다음과 같다.
 * 1. 공백과 주석을 건너뛴다.
 * 2. 식별자/예약어, 숫자, 문자열을 읽는다.
 * 3. 구두점(* , ( ) ; = .)을 토큰으로 만든다.
 * 4. 마지막에 EOF 토큰을 하나 추가한다.
 */
bool tokenize_sql(const char *input, TokenList *out_tokens, ErrorContext *err) {
    /* 현재 읽고 있는 원문 위치 포인터다. */
    const char *cursor = input;
    /* 현재 줄 번호다. */
    int line   = 1;
    /* 현재 열 번호다. */
    int column = 1;

    /* 출력 토큰 배열을 빈 상태로 초기화한다. */
    out_tokens->items    = NULL;
    /* 현재 토큰 개수를 0으로 초기화한다. */
    out_tokens->count    = 0U;
    /* 현재 용량도 0으로 초기화한다. */
    out_tokens->capacity = 0U;

    /* 원문 끝을 만날 때까지 한 글자씩 토큰화를 진행한다. */
    while (*cursor != '\0') {
        /* 현재 보고 있는 문자다. */
        char current      = *cursor;
        /* 지금 토큰이 시작된 줄 번호를 따로 저장한다. */
        int token_line    = line;
        /* 지금 토큰이 시작된 열 번호를 따로 저장한다. */
        int token_column  = column;

        /* 1. 의미 없는 공백 문자는 건너뛴다. */
        if (current == ' ' || current == '\t' || current == '\r') {
            cursor += 1;
            column += 1;
            continue;
        }

        /* 줄바꿈은 토큰을 만들지 않지만 위치 정보는 갱신한다. */
        if (current == '\n') {
            cursor += 1;
            line   += 1;
            column  = 1;
            continue;
        }

        /* SQL 한 줄 주석은 줄 끝까지 무시한다. */
        if (current == '-' && cursor[1] == '-') {
            cursor += 2;
            column += 2;
            while (*cursor != '\0' && *cursor != '\n') {
                cursor += 1;
                column += 1;
            }
            continue;
        }

        /* 2. 문자/언더스코어로 시작하면 식별자 또는 예약어다. */
        if (isalpha((unsigned char) current) || current == '_') {
            /* 식별자가 시작된 위치다. */
            const char *start = cursor;
            /* 식별자 길이다. */
            size_t length;
            /* 식별자 복사본 문자열이다. */
            char *identifier_text;
            /* 예약어인지 일반 식별자인지 판단한 토큰 종류다. */
            TokenType type;

            /* 영문자, 숫자, 밑줄이 이어지는 동안 계속 읽는다. */
            while (isalnum((unsigned char) *cursor) || *cursor == '_') {
                cursor += 1;
                column += 1;
            }

            /* 읽은 길이를 계산한다. */
            length = (size_t) (cursor - start);
            /* 식별자 문자열 복사본을 위한 메모리를 잡는다. */
            identifier_text = malloc(length + 1U);
            /* 할당 실패 시 지금까지 만든 토큰까지 정리하고 중단한다. */
            if (identifier_text == NULL) {
                set_error(err, "SQL 토큰화 중 메모리가 부족합니다");
                free_token_fields(out_tokens);
                return false;
            }
            /* 식별자 원문을 복사한다. */
            memcpy(identifier_text, start, length);
            /* 끝에 널 종료 문자를 붙인다. */
            identifier_text[length] = '\0';
            /* 예약어인지 일반 식별자인지 판정한다. */
            type = keyword_type(identifier_text);

            /* 완성된 식별자 토큰을 토큰 배열 뒤에 붙인다. */
            if (!push_token_owned(out_tokens, type, identifier_text, token_line, token_column, err)) {
                free_token_fields(out_tokens);
                return false;
            }
            continue;
        }

        /* 숫자로 시작하면 정수 또는 단순 소수 형태 숫자로 읽는다. */
        if (isdigit((unsigned char) current)) {
            /* 숫자 토큰이 시작된 위치다. */
            const char *start = cursor;

            /* 정수 부분을 모두 읽는다. */
            while (isdigit((unsigned char) *cursor)) {
                cursor += 1;
                column += 1;
            }

            /* 소수점 뒤에도 숫자가 있으면 단순 소수로 계속 읽는다. */
            if (*cursor == '.' && isdigit((unsigned char) cursor[1])) {
                cursor += 1;
                column += 1;
                while (isdigit((unsigned char) *cursor)) {
                    cursor += 1;
                    column += 1;
                }
            }

            /* 읽은 범위를 NUMBER 토큰으로 저장한다. */
            if (!push_token_copy(out_tokens, TOKEN_NUMBER, start, (size_t) (cursor - start), token_line, token_column,
                                 err)) {
                free_token_fields(out_tokens);
                return false;
            }
            continue;
        }

        /* 작은따옴표는 문자열 리터럴의 시작이다. */
        if (current == '\'') {
            if (!scan_string_literal(&cursor, &line, &column, out_tokens, err)) {
                free_token_fields(out_tokens);
                return false;
            }
            continue;
        }

        /* 3. 나머지는 한 글자짜리 구두점 토큰으로 처리한다. */
        switch (current) {
            case '*': case ',': case '(': case ')': case ';': case '=': case '.': {
                /* 현재 문자에 대응하는 구두점 토큰 종류다. */
                TokenType punct_type;
                /* 문자 하나를 어떤 TokenType 으로 볼지 결정한다. */
                switch (current) {
                    case '*': punct_type = TOKEN_STAR;      break;
                    case ',': punct_type = TOKEN_COMMA;     break;
                    case '(': punct_type = TOKEN_LPAREN;    break;
                    case ')': punct_type = TOKEN_RPAREN;    break;
                    case ';': punct_type = TOKEN_SEMICOLON; break;
                    case '=': punct_type = TOKEN_EQUALS;    break;
                    default:  punct_type = TOKEN_DOT;       break;
                }
                /* 구두점 하나를 길이 1 토큰으로 저장한다. */
                if (!push_token_copy(out_tokens, punct_type, cursor, 1U, token_line, token_column, err)) {
                    free_token_fields(out_tokens);
                    return false;
                }
                break;
            }
            default:
                /* 지원하지 않는 문자를 만나면 위치와 함께 오류를 반환한다. */
                set_error(err, "%d:%d 위치에 예상하지 못한 문자가 있습니다: '%c'",
                          token_line, token_column, current);
                free_token_fields(out_tokens);
                return false;
        }

        /* 한 글자짜리 구두점은 여기서 입력 위치를 한 칸 전진시킨다. */
        cursor += 1;
        /* 열 위치도 한 칸 전진한다. */
        column += 1;
    }

    /* 4. parser 가 입력 끝을 감지할 수 있도록 EOF 토큰을 덧붙인다. */
    if (!push_token_copy(out_tokens, TOKEN_EOF, "", 0U, line, column, err)) {
        free_token_fields(out_tokens);
        return false;
    }

    return true;
}

/* 외부 모듈이 호출하는 공개 해제 함수다. */
void free_token_list(TokenList *tokens) {
    /* NULL 이면 정리할 대상이 없다. */
    if (tokens == NULL) {
        return;
    }

    /* 내부 공통 해제 함수로 실제 정리를 수행한다. */
    free_token_fields(tokens);
}
