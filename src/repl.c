#include "repl.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

typedef struct LineHistory {
    char **items;
    size_t count;
    size_t capacity;
} LineHistory;

static struct termios g_original_termios;
static bool g_raw_mode_enabled = false;

static void disable_raw_mode(void) {
    if (!g_raw_mode_enabled) {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_original_termios);
    g_raw_mode_enabled = false;
}

static void handle_signal(int signal_number) {
    disable_raw_mode();
    _exit(128 + signal_number);
}

static bool enable_raw_mode(char *error_buf, size_t error_size) {
    struct termios raw;

    if (g_raw_mode_enabled) {
        return true;
    }

    if (tcgetattr(STDIN_FILENO, &g_original_termios) != 0) {
        set_error(error_buf, error_size, "failed to read terminal attributes");
        return false;
    }

    raw = g_original_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        set_error(error_buf, error_size, "failed to enable raw terminal mode");
        return false;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGQUIT, handle_signal);
    atexit(disable_raw_mode);
    g_raw_mode_enabled = true;
    return true;
}

static bool ensure_buffer_capacity(char **buffer, size_t *capacity, size_t required,
                                   char *error_buf, size_t error_size) {
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
        set_error(error_buf, error_size, "out of memory while handling CLI input");
        return false;
    }

    *buffer = new_buffer;
    *capacity = new_capacity;
    return true;
}

static bool replace_buffer_text(char **buffer, size_t *length, size_t *capacity, const char *text,
                                char *error_buf, size_t error_size) {
    size_t text_length = strlen(text);

    if (!ensure_buffer_capacity(buffer, capacity, text_length + 1U, error_buf, error_size)) {
        return false;
    }

    memcpy(*buffer, text, text_length + 1U);
    *length = text_length;
    return true;
}

static bool append_text(char **buffer, size_t *length, size_t *capacity, const char *text,
                        char *error_buf, size_t error_size) {
    size_t text_length = strlen(text);
    size_t required = *length + text_length + 1U;

    if (!ensure_buffer_capacity(buffer, capacity, required, error_buf, error_size)) {
        return false;
    }

    memcpy(*buffer + *length, text, text_length + 1U);
    *length += text_length;
    return true;
}

static void free_history(LineHistory *history) {
    free_string_array(history->items, history->count);
    history->items = NULL;
    history->count = 0U;
    history->capacity = 0U;
}

static bool push_history(LineHistory *history, const char *line, char *error_buf, size_t error_size) {
    char **new_items;
    char *copy;
    size_t new_capacity;

    if (line[0] == '\0') {
        return true;
    }

    if (history->count > 0U && strcmp(history->items[history->count - 1U], line) == 0) {
        return true;
    }

    if (history->count >= history->capacity) {
        new_capacity = history->capacity == 0U ? 16U : history->capacity * 2U;
        new_items = realloc(history->items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            set_error(error_buf, error_size, "out of memory while storing command history");
            return false;
        }
        history->items = new_items;
        history->capacity = new_capacity;
    }

    copy = msql_strdup(line);
    if (copy == NULL) {
        set_error(error_buf, error_size, "out of memory while storing command history");
        return false;
    }

    history->items[history->count] = copy;
    history->count += 1U;
    return true;
}

static const char *trim_start(const char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text += 1;
    }

    return text;
}

static bool is_blank_text(const char *text) {
    return *trim_start(text) == '\0';
}

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

static char *read_line_interactive(const char *prompt, LineHistory *history, char *error_buf, size_t error_size) {
    char *buffer = NULL;
    size_t length = 0U;
    size_t capacity = 0U;
    size_t cursor = 0U;
    size_t history_index = history->count;
    char *scratch = NULL;

    if (!ensure_buffer_capacity(&buffer, &capacity, 1U, error_buf, error_size)) {
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

        if (ch == '\r' || ch == '\n') {
            printf("\r\n");
            fflush(stdout);
            break;
        }

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

        if (ch == 1) {
            cursor = 0U;
            refresh_line(prompt, buffer, length, cursor);
            continue;
        }

        if (ch == 5) {
            cursor = length;
            refresh_line(prompt, buffer, length, cursor);
            continue;
        }

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

            if (seq[1] == 'A') {
                if (history->count == 0U || history_index == 0U) {
                    continue;
                }
                if (history_index == history->count) {
                    free(scratch);
                    scratch = msql_strdup(buffer);
                    if (scratch == NULL) {
                        free(buffer);
                        set_error(error_buf, error_size, "out of memory while navigating command history");
                        return NULL;
                    }
                }
                history_index -= 1U;
                if (!replace_buffer_text(&buffer, &length, &capacity, history->items[history_index], error_buf, error_size)) {
                    free(buffer);
                    free(scratch);
                    return NULL;
                }
                cursor = length;
                refresh_line(prompt, buffer, length, cursor);
                continue;
            }

            if (seq[1] == 'B') {
                if (history_index >= history->count) {
                    continue;
                }
                history_index += 1U;
                if (history_index == history->count) {
                    if (!replace_buffer_text(&buffer, &length, &capacity, scratch == NULL ? "" : scratch, error_buf, error_size)) {
                        free(buffer);
                        free(scratch);
                        return NULL;
                    }
                } else if (!replace_buffer_text(&buffer, &length, &capacity, history->items[history_index], error_buf, error_size)) {
                    free(buffer);
                    free(scratch);
                    return NULL;
                }
                cursor = length;
                refresh_line(prompt, buffer, length, cursor);
                continue;
            }

            if (seq[1] == 'C') {
                if (cursor < length) {
                    cursor += 1U;
                    refresh_line(prompt, buffer, length, cursor);
                }
                continue;
            }

            if (seq[1] == 'D') {
                if (cursor > 0U) {
                    cursor -= 1U;
                    refresh_line(prompt, buffer, length, cursor);
                }
                continue;
            }

            if (seq[1] == 'H') {
                cursor = 0U;
                refresh_line(prompt, buffer, length, cursor);
                continue;
            }

            if (seq[1] == 'F') {
                cursor = length;
                refresh_line(prompt, buffer, length, cursor);
                continue;
            }

            continue;
        }

        if (!ensure_buffer_capacity(&buffer, &capacity, length + 2U, error_buf, error_size)) {
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

    if (!is_blank_text(buffer) && !push_history(history, buffer, error_buf, error_size)) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

bool run_repl(MiniSqlApp *app, char *error_buf, size_t error_size) {
    char *statement = NULL;
    size_t statement_length = 0U;
    size_t statement_capacity = 0U;
    bool interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    LineHistory history = {0};

    if (interactive) {
        if (!enable_raw_mode(error_buf, error_size)) {
            return false;
        }
        printf("Mini SQL interactive mode\n");
        printf("Type SQL and end each statement with ';'\n");
        printf("Use .exit to quit\n");
    }

    while (true) {
        char *line;
        const char *prompt = statement_length == 0U ? "mini_sql> " : "       -> ";

        /* 1. 한 줄 입력을 받는다. 인터랙티브 모드면 편집 가능한 REPL 입력기를 쓰고,
         *    비인터랙티브 모드면 공용 스트림 라인 리더로 stdin을 읽는다.
         */
        if (interactive) {
            line = read_line_interactive(prompt, &history, error_buf, error_size);
        } else {
            line = read_stream_line(stdin);
        }
        if (line == NULL) {
            break;
        }

        /* 2. 문장이 비어 있는 상태에서 종료 명령을 받으면 REPL을 끝낸다. */
        if (statement_length == 0U && is_exit_command(line)) {
            free(line);
            break;
        }

        /* 3. 여러 줄 SQL을 지원하기 위해 현재 줄을 누적 버퍼에 붙인다. */
        if (!append_text(&statement, &statement_length, &statement_capacity, line, error_buf, error_size)) {
            free(line);
            free_history(&history);
            disable_raw_mode();
            free(statement);
            return false;
        }
        if (line[0] == '\0' || line[strlen(line) - 1U] != '\n') {
            if (!append_text(&statement, &statement_length, &statement_capacity, "\n", error_buf, error_size)) {
                free(line);
                free_history(&history);
                disable_raw_mode();
                free(statement);
                return false;
            }
        }
        free(line);

        /* 4. 세미콜론이 아직 없으면 다음 줄을 더 받아서 같은 SQL 문장을 완성한다. */
        if (!ends_with_semicolon(statement)) {
            continue;
        }

        /* 5. 문장이 완성되면 tokenize -> parse -> execute 파이프라인으로 넘긴다. */
        if (!mini_sql_app_run_sql(app, statement, error_buf, error_size)) {
            fprintf(stderr, "Error: %s\n", error_buf);
        }

        /* 6. 현재 문장 버퍼를 비워 다음 SQL 입력을 받을 준비를 한다. */
        statement[0] = '\0';
        statement_length = 0U;
    }

    /* 7. EOF로 종료됐지만 세미콜론 없이 남은 SQL이 있으면 마지막으로 한 번 더 처리한다. */
    if (statement != NULL && !is_blank_text(statement)) {
        if (!mini_sql_app_run_sql(app, statement, error_buf, error_size)) {
            fprintf(stderr, "Error: %s\n", error_buf);
            free(statement);
            return false;
        }
    }

    free_history(&history);
    disable_raw_mode();
    free(statement);
    return true;
}
