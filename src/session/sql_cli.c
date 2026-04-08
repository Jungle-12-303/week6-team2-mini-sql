#include "session/sql_cli.h"
#include "session/history.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>

/*
 * sql_cli.c 는 인터랙티브 SQL CLI 를 담당한다.
 *
 * 이 파일의 역할은 SQL 을 직접 실행하는 것이 아니라,
 * 사용자의 키 입력을 편집 가능한 한 줄 입력으로 받아 세미콜론 단위 문장으로 모은 뒤
 * SqlInput 으로 감싸 SqlSession 으로 넘기는 것이다.
 */

/* raw mode 를 켜기 전 원래 터미널 설정을 보관한다. */
static struct termios g_original_termios;
static bool g_raw_mode_enabled = false;

/* 프로그램 종료 전 터미널을 원래 상태로 돌린다. */
static void disable_raw_mode(void) {
    if (!g_raw_mode_enabled) {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_original_termios);
    g_raw_mode_enabled = false;
}

/* 시그널이 와도 터미널이 깨진 상태로 남지 않도록 정리 후 종료한다. */
static void handle_signal(int signal_number) {
    disable_raw_mode();
    _exit(128 + signal_number);
}

/* 방향키/커서 이동이 가능한 입력기를 위해 터미널 raw mode 를 켠다. */
static bool enable_raw_mode(ErrorContext *err) {
    struct termios raw;

    if (g_raw_mode_enabled) {
        return true;
    }

    if (tcgetattr(STDIN_FILENO, &g_original_termios) != 0) {
        set_error(err, "터미널 속성을 읽지 못했습니다");
        return false;
    }

    raw = g_original_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        set_error(err, "터미널 raw 모드를 켜지 못했습니다");
        return false;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGQUIT, handle_signal);
    atexit(disable_raw_mode);
    g_raw_mode_enabled = true;
    return true;
}

/* 입력 버퍼에 새 문자를 넣기 전에 충분한 공간을 확보한다. */
static bool ensure_buffer_capacity(char **buffer, size_t *capacity, size_t required,
                                   ErrorContext *err) {
    size_t new_capacity;
    char *new_buffer;

    if (required <= *capacity) {
        return true;
    }

    new_capacity = *capacity == 0U ? 256U : *capacity;
    while (new_capacity < required) {
        new_capacity *= 2U;
    }

    new_buffer = realloc(*buffer, new_capacity);
    if (new_buffer == NULL) {
        set_error(err, "CLI 입력을 처리하는 중 메모리가 부족합니다");
        return false;
    }

    *buffer = new_buffer;
    *capacity = new_capacity;
    return true;
}

/* 히스토리 탐색 결과처럼 버퍼 전체를 다른 문자열로 교체할 때 사용한다. */
static bool replace_buffer_text(char **buffer, size_t *length, size_t *capacity, const char *text,
                                ErrorContext *err) {
    size_t text_length = strlen(text);

    if (!ensure_buffer_capacity(buffer, capacity, text_length + 1U, err)) {
        return false;
    }

    memcpy(*buffer, text, text_length + 1U);
    *length = text_length;
    return true;
}

/* 여러 줄 SQL 누적 버퍼 뒤에 텍스트를 그대로 덧붙인다. */
static bool append_text(char **buffer, size_t *length, size_t *capacity, const char *text,
                        ErrorContext *err) {
    size_t text_length = strlen(text);
    size_t required = *length + text_length + 1U;

    if (!ensure_buffer_capacity(buffer, capacity, required, err)) {
        return false;
    }

    memcpy(*buffer + *length, text, text_length + 1U);
    *length += text_length;
    return true;
}

/* 앞쪽 공백을 건너뛴 시작 위치를 반환한다. */
static const char *trim_start(const char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text += 1;
    }

    return text;
}

/* 입력 줄이 공백뿐인지 판별한다. */
static bool is_blank_text(const char *text) {
    return *trim_start(text) == '\0';
}

/* 현재까지 누적된 SQL 문장이 세미콜론으로 끝나는지 확인한다. */
static bool ends_with_semicolon(const char *text) {
    size_t length = strlen(text);

    while (length > 0U) {
        char current = text[length - 1U];

        if (current == ' ' || current == '\t' || current == '\r' || current == '\n') {
            length -= 1U;
            continue;
        }

        return current == ';';
    }

    return false;
}

/* .exit / exit / quit 같은 종료 명령을 판별한다. */
static bool is_exit_command(const char *line) {
    const char *trimmed = trim_start(line);
    size_t length = strlen(trimmed);

    while (length > 0U &&
           (trimmed[length - 1U] == ' ' || trimmed[length - 1U] == '\t' ||
            trimmed[length - 1U] == '\r' || trimmed[length - 1U] == '\n')) {
        length -= 1U;
    }

    return (length == 5U && strncmp(trimmed, ".exit", 5U) == 0) ||
           (length == 4U && strncmp(trimmed, "exit", 4U) == 0) ||
           (length == 4U && strncmp(trimmed, "quit", 4U) == 0);
}

/* .clear 를 입력하면 현재까지 누적한 SQL 문장을 버린다. */
static bool is_clear_command(const char *line) {
    const char *trimmed = trim_start(line);
    size_t length = strlen(trimmed);

    while (length > 0U &&
           (trimmed[length - 1U] == ' ' || trimmed[length - 1U] == '\t' ||
            trimmed[length - 1U] == '\r' || trimmed[length - 1U] == '\n')) {
        length -= 1U;
    }

    return length == 6U && strncmp(trimmed, ".clear", 6U) == 0;
}

/* 새 문장의 첫 단어가 지원하는 SQL 시작 키워드인지 확인한다. */
static bool starts_with_supported_sql_keyword(const char *line) {
    const char *trimmed = trim_start(line);
    size_t length = 0U;

    while ((trimmed[length] >= 'A' && trimmed[length] <= 'Z') ||
           (trimmed[length] >= 'a' && trimmed[length] <= 'z')) {
        length += 1U;
    }

    if (length == 0U) {
        return false;
    }

    return (length == 6U && strncasecmp(trimmed, "INSERT", 6U) == 0) ||
           (length == 6U && strncasecmp(trimmed, "SELECT", 6U) == 0) ||
           (length == 6U && strncasecmp(trimmed, "CREATE", 6U) == 0) ||
           (length == 4U && strncasecmp(trimmed, "DROP", 4U) == 0) ||
           (length == 6U && strncasecmp(trimmed, "DELETE", 6U) == 0);
}

/* CLI 에서 완성된 SQL 문장을 SqlInput 으로 감싸 공통 실행 경로로 넘긴다. */
static bool execute_cli_statement(SqlSession *session, const char *statement, ErrorContext *err) {
    SqlInput input;

    input.kind = SQL_INPUT_CLI;
    input.source_name = "대화형 CLI";
    input.text = statement;
    return sql_session_execute(session, &input, err);
}

static bool validate_cli_session(SqlSession *session, ErrorContext *err) {
    if (session == NULL) {
        set_error(err, "SQL 세션이 올바르지 않습니다");
        return false;
    }

    return true;
}

static bool validate_cli_terminal(ErrorContext *err) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        set_error(err, "대화형 모드는 TTY 터미널이 필요합니다. 터미널에서 실행하거나 .sql 파일을 넘겨주세요");
        return false;
    }

    return true;
}

static void print_cli_banner(void) {
    printf("Mini SQL 대화형 모드\n");
    printf("SQL을 입력하고 각 문장은 ';'로 끝내주세요\n");
    printf(".exit 를 입력하면 종료합니다\n");
    printf(".clear 를 입력하면 현재 입력 중인 문장을 지웁니다\n");
}

static const char *cli_prompt(size_t statement_length) {
    return statement_length == 0U ? "mini_sql> " : "       -> ";
}

static bool append_line_break_if_needed(char **statement, size_t *statement_length,
                                        size_t *statement_capacity, const char *line, ErrorContext *err) {
    if (line[0] == '\0' || line[strlen(line) - 1U] != '\n') {
        return append_text(statement, statement_length, statement_capacity, "\n", err);
    }

    return true;
}

static bool append_cli_line_to_statement(char **statement, size_t *statement_length,
                                         size_t *statement_capacity, const char *line, ErrorContext *err) {
    if (!append_text(statement, statement_length, statement_capacity, line, err)) {
        return false;
    }

    return append_line_break_if_needed(statement, statement_length, statement_capacity, line, err);
}

static void clear_statement_buffer(char *statement, size_t *statement_length) {
    if (statement == NULL) {
        return;
    }

    statement[0] = '\0';
    *statement_length = 0U;
}

static void print_runtime_error(const ErrorContext *err) {
    fprintf(stderr, "오류: %s\n", err->buf);
}

static void print_ignored_input_message(void) {
    fprintf(stderr, "안내: 새 문장은 INSERT, SELECT, CREATE, DROP, DELETE 중 하나로 시작해야 합니다\n");
}

static void print_statement_cleared_message(void) {
    fprintf(stderr, "안내: 현재 입력 중이던 SQL 문장을 지웠습니다\n");
}

static bool execute_completed_statement(SqlSession *session, char *statement, size_t *statement_length,
                                        ErrorContext *err) {
    bool ok = execute_cli_statement(session, statement, err);

    if (!ok) {
        print_runtime_error(err);
    }

    clear_statement_buffer(statement, statement_length);
    return ok;
}

static void cleanup_cli_runtime(LineHistory *history, char *statement) {
    history_free(history);
    disable_raw_mode();
    free(statement);
}

static bool handle_append_failure(LineHistory *history, char *statement) {
    cleanup_cli_runtime(history, statement);
    return false;
}

static bool flush_remaining_statement(SqlSession *session, char *statement, ErrorContext *err) {
    if (statement == NULL || is_blank_text(statement)) {
        return true;
    }

    if (!execute_cli_statement(session, statement, err)) {
        print_runtime_error(err);
        return false;
    }

    return true;
}

/* 프롬프트, 현재 버퍼, 커서 위치를 기준으로 한 줄 화면을 다시 그린다. */
static void refresh_line(const char *prompt, const char *buffer, size_t length, size_t cursor) {
    printf("\r%s", prompt);
    if (length > 0U) {
        fwrite(buffer, 1U, length, stdout);
    }
    printf("\x1b[K");
    printf("\r%s", prompt);
    if (cursor > 0U) {
        fwrite(buffer, 1U, cursor, stdout);
    }
    fflush(stdout);
}

/*
 * 인터랙티브 한 줄 입력기다.
 * 문자 입력, 백스페이스, 방향키, 홈/엔드, 히스토리 탐색을 모두 여기서 처리한다.
 */
static char *read_line_interactive(const char *prompt, LineHistory *history, ErrorContext *err) {
    char *buffer = NULL;
    size_t length = 0U;
    size_t capacity = 0U;
    size_t cursor = 0U;
    size_t history_index = history->count;
    char *scratch = NULL;

    if (!ensure_buffer_capacity(&buffer, &capacity, 1U, err)) {
        return NULL;
    }
    buffer[0] = '\0';

    refresh_line(prompt, buffer, length, cursor);

    while (true) {
        char ch;
        ssize_t read_size = read(STDIN_FILENO, &ch, 1U);

        if (read_size <= 0) {
            free(buffer);
            free(scratch);
            return NULL;
        }

        /* Enter 를 누르면 현재 줄 입력이 끝난다. */
        if (ch == '\r' || ch == '\n') {
            printf("\r\n");
            fflush(stdout);
            break;
        }

        /* Ctrl-D 는 빈 줄이면 EOF 종료, 아니면 무시한다. */
        if (ch == 4) {
            if (length == 0U) {
                printf("\r\n");
                fflush(stdout);
                free(buffer);
                free(scratch);
                return NULL;
            }
            continue;
        }

        /* Backspace/Delete 는 커서 왼쪽 문자를 지운다. */
        if (ch == 127 || ch == 8) {
            if (cursor > 0U) {
                memmove(buffer + cursor - 1U, buffer + cursor, length - cursor + 1U);
                cursor -= 1U;
                length -= 1U;
                if (history_index != history->count) {
                    history_index = history->count;
                }
                refresh_line(prompt, buffer, length, cursor);
            }
            continue;
        }

        /* Ctrl-A: 줄 맨 앞으로 이동 */
        if (ch == 1) {
            cursor = 0U;
            refresh_line(prompt, buffer, length, cursor);
            continue;
        }

        /* Ctrl-E: 줄 맨 끝으로 이동 */
        if (ch == 5) {
            cursor = length;
            refresh_line(prompt, buffer, length, cursor);
            continue;
        }

        /* ESC 시퀀스는 방향키/홈/엔드/삭제 키를 처리한다. */
        if (ch == 27) {
            char seq[3];

            if (read(STDIN_FILENO, &seq[0], 1U) <= 0 || read(STDIN_FILENO, &seq[1], 1U) <= 0) {
                continue;
            }

            if (seq[0] == '[' && seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1U) <= 0) {
                    continue;
                }

                if (seq[1] == '3' && seq[2] == '~' && cursor < length) {
                    memmove(buffer + cursor, buffer + cursor + 1U, length - cursor);
                    length -= 1U;
                    refresh_line(prompt, buffer, length, cursor);
                }
                continue;
            }

            if (seq[0] != '[') {
                continue;
            }

            /* Up: 이전 히스토리 */
            if (seq[1] == 'A') {
                if (history->count == 0U || history_index == 0U) {
                    continue;
                }
                if (history_index == history->count) {
                    free(scratch);
                    scratch = msql_strdup(buffer);
                    if (scratch == NULL) {
                        free(buffer);
                        set_error(err, "명령 히스토리를 탐색하는 중 메모리가 부족합니다");
                        return NULL;
                    }
                }
                history_index -= 1U;
                if (!replace_buffer_text(&buffer, &length, &capacity, history_get(history, history_index), err)) {
                    free(buffer);
                    free(scratch);
                    return NULL;
                }
                cursor = length;
                refresh_line(prompt, buffer, length, cursor);
                continue;
            }

            /* Down: 다음 히스토리 또는 원래 편집 중이던 입력 복원 */
            if (seq[1] == 'B') {
                if (history_index >= history->count) {
                    continue;
                }
                history_index += 1U;
                if (history_index == history->count) {
                    if (!replace_buffer_text(&buffer, &length, &capacity, scratch == NULL ? "" : scratch, err)) {
                        free(buffer);
                        free(scratch);
                        return NULL;
                    }
                } else if (!replace_buffer_text(&buffer, &length, &capacity, history_get(history, history_index), err)) {
                    free(buffer);
                    free(scratch);
                    return NULL;
                }
                cursor = length;
                refresh_line(prompt, buffer, length, cursor);
                continue;
            }

            /* Right: 커서를 오른쪽으로 한 칸 이동 */
            if (seq[1] == 'C') {
                if (cursor < length) {
                    cursor += 1U;
                    refresh_line(prompt, buffer, length, cursor);
                }
                continue;
            }

            /* Left: 커서를 왼쪽으로 한 칸 이동 */
            if (seq[1] == 'D') {
                if (cursor > 0U) {
                    cursor -= 1U;
                    refresh_line(prompt, buffer, length, cursor);
                }
                continue;
            }

            /* Home */
            if (seq[1] == 'H') {
                cursor = 0U;
                refresh_line(prompt, buffer, length, cursor);
                continue;
            }

            /* End */
            if (seq[1] == 'F') {
                cursor = length;
                refresh_line(prompt, buffer, length, cursor);
                continue;
            }

            continue;
        }

        /* 일반 문자는 커서 위치에 삽입한다. */
        if (!ensure_buffer_capacity(&buffer, &capacity, length + 2U, err)) {
            free(buffer);
            free(scratch);
            return NULL;
        }

        memmove(buffer + cursor + 1U, buffer + cursor, length - cursor + 1U);
        buffer[cursor] = ch;
        cursor += 1U;
        length += 1U;
        history_index = history->count;
        refresh_line(prompt, buffer, length, cursor);
    }

    free(scratch);

    if (!is_blank_text(buffer) && !history_push(history, buffer, err)) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

/*
 * 프로그램 전체의 인터랙티브 SQL CLI 루프다.
 *
 * 핵심 흐름:
 * 1. 한 줄을 읽는다.
 * 2. 여러 줄 SQL 을 위해 누적 버퍼에 붙인다.
 * 3. 세미콜론이 나오면 SQL 한 문장이 완성된 것으로 간주한다.
 * 4. 완성된 문장을 SqlSession 계층으로 넘긴다.
 * 5. 다음 문장을 위해 누적 버퍼를 비운다.
 */
bool run_sql_cli(SqlSession *session, ErrorContext *err) {
    char *statement = NULL;
    size_t statement_length = 0U;
    size_t statement_capacity = 0U;
    LineHistory history = {0};

    if (!validate_cli_session(session, err)) {
        return false;
    }

    if (!validate_cli_terminal(err)) {
        return false;
    }

    if (!enable_raw_mode(err)) {
        return false;
    }

    print_cli_banner();

    while (true) {
        char *line;
        const char *prompt = cli_prompt(statement_length);

        /* 1. 편집 가능한 인터랙티브 입력기에서 한 줄을 받는다. */
        line = read_line_interactive(prompt, &history, err);
        if (line == NULL) {
            break;
        }

        /* 2. 문장이 비어 있는 상태에서 종료 명령을 받으면 SQL CLI를 끝낸다. */
        if (statement_length == 0U && is_exit_command(line)) {
            free(line);
            break;
        }

        /* 2-1. 줄이 비어 있고 아직 시작한 SQL 문장이 없으면 그냥 무시한다. */
        if (statement_length == 0U && is_blank_text(line)) {
            free(line);
            continue;
        }

        /* 2-2. .clear 는 현재까지 누적한 SQL 문장을 즉시 비운다. */
        if (is_clear_command(line)) {
            clear_statement_buffer(statement, &statement_length);
            print_statement_cleared_message();
            free(line);
            continue;
        }

        /*
         * 2-3. 새 문장의 첫 줄은 지원하는 SQL 키워드로 시작해야 한다.
         * 그렇지 않으면 버퍼를 오염시키지 않도록 이번 줄을 무시한다.
         */
        if (statement_length == 0U && !starts_with_supported_sql_keyword(line)) {
            print_ignored_input_message();
            free(line);
            continue;
        }

        /* 3. 여러 줄 SQL을 지원하기 위해 현재 줄을 누적 버퍼에 붙인다. */
        if (!append_cli_line_to_statement(&statement, &statement_length, &statement_capacity, line, err)) {
            free(line);
            return handle_append_failure(&history, statement);
        }
        free(line);

        /* 4. 세미콜론이 아직 없으면 다음 줄을 더 받아서 같은 SQL 문장을 완성한다. */
        if (!ends_with_semicolon(statement)) {
            continue;
        }

        /* 5. 문장이 완성되면 tokenize -> parse -> execute 파이프라인으로 넘긴다. */
        execute_completed_statement(session, statement, &statement_length, err);
    }

    /* 7. EOF로 종료됐지만 세미콜론 없이 남은 SQL이 있으면 마지막으로 한 번 더 처리한다. */
    if (!flush_remaining_statement(session, statement, err)) {
        cleanup_cli_runtime(&history, statement);
        return false;
    }

    cleanup_cli_runtime(&history, statement);
    return true;
}
