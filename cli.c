#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "parser.h"
#include "executor.h"
#include "storage.h"

/* This trims the input line so CLI commands can compare clean strings. */
static void trim_line(char *line) {
    int end = (int)strlen(line) - 1;

    while (end >= 0 && (line[end] == '\n' || line[end] == '\r' || line[end] == ' ' || line[end] == '\t')) {
        line[end--] = '\0';
    }
}

/* This prints help because the REPL needs discoverable commands. */
static void print_help(void) {
    printf(".help\n");
    printf(".tables\n");
    printf(".schema users\n");
    printf(".exit\n");
    printf("INSERT INTO users VALUES (1, 'bumsang', 25);\n");
    printf("SELECT * FROM users;\n");
    printf("SELECT id, name FROM users;\n");
}

/* This handles dot commands so SQL parsing stays focused on SQL only. */
static int handle_meta_command(const char *line) {
    char table_name[32];

    if (strcmp(line, ".exit") == 0) {
        return 0;
    }
    if (strcmp(line, ".help") == 0) {
        print_help();
        return 1;
    }
    if (strcmp(line, ".tables") == 0) {
        list_tables();
        return 1;
    }
    if (sscanf(line, ".schema %31s", table_name) == 1) {
        if (!print_schema(table_name)) {
            printf("table not found\n");
        }
        return 1;
    }

    printf("unknown command\n");  
    return 1;
}

/* This runs the REPL because the program now works like a tiny SQL shell. */
void run_cli(void) {
    char line[256];
    Query query;

    while (1) {
        /* 1. Read one input line */
        printf("mini-sql> ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }
        trim_line(line);
        if (line[0] == '\0') {
            continue;
        }

        /* 2. Handle meta commands or SQL */
        if (line[0] == '.') {
            if (!handle_meta_command(line)) {
                break;
            }
            continue;
        }

        /* 3. Parse and execute SQL */
        if (!parse_query(line, &query)) {
            printf("syntax error\n");
            continue;
        }
        execute_query(&query);
    }
}
