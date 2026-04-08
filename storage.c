#include <stdio.h>
#include <string.h>
#include "storage.h"
int append_user(const Query *query) {
    FILE *file = fopen("users.data", "a");

    if (file == NULL) {
        return 0;
    }
    fprintf(file, "%d|%s|%d\n", query->id, query->name, query->age);
    fclose(file);
    printf("1 row inserted\n");
    return 1;
}

static void print_selected_row(const Query *query, int id, const char *name, int age) {
    int i;

    for (i = 0; i < query->selected_column_count; i++) {
        if (i > 0) {
            printf("|");
        }
        if (strcmp(query->selected_columns[i], "id") == 0) {
            printf("%d", id);
        } else if (strcmp(query->selected_columns[i], "name") == 0) {
            printf("%s", name);
        } else if (strcmp(query->selected_columns[i], "age") == 0) {
            printf("%d", age);
        }
    }
    printf("\n");
}

int select_users(const Query *query) {
    FILE *file = fopen("users.data", "r");
    char line[128];
    int id;
    int age;
    char name[32];

    if (file == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "%d|%31[^|]|%d", &id, name, &age) != 3) {
            continue;
        }
        if (query->select_all) {
            printf("%d|%s|%d\n", id, name, age);
        } else {
            print_selected_row(query, id, name, age);
        }
    }

    fclose(file);
    return 1;
}
