#include <stdio.h>
#include <string.h>
#include "storage.h"

/* This builds simple schema/data file names from a table name. */
static void make_filename(const char *table_name, const char *ext, char *path, int size) {
    snprintf(path, size, "%s.%s", table_name, ext);
}

/* This loads schema so SELECT and .schema know the column layout. */
int load_schema(const char *table_name, TableSchema *schema) {
    FILE *file;
    char path[64];
    char line[64];

    make_filename(table_name, "schema", path, sizeof(path));
    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }

    schema->column_count = 0;
    while (fgets(line, sizeof(line), file) != NULL && schema->column_count < MAX_COLUMNS) {
        if (sscanf(line, "%31[^|]|%15s",
                   schema->names[schema->column_count],
                   schema->types[schema->column_count]) == 2) {
            schema->column_count++;
        }
    }

    fclose(file);
    return schema->column_count > 0;
}

/* This loads table rows so display can print one consistent result set. */
int load_rows(const char *table_name, TableData *data) {
    FILE *file;
    char path[64];
    char line[128];
    char id[32];
    char name[32];
    char age[32];

    make_filename(table_name, "data", path, sizeof(path));
    file = fopen(path, "r");
    data->row_count = 0;
    if (file == NULL) {
        return 1;
    }

    while (fgets(line, sizeof(line), file) != NULL && data->row_count < MAX_ROWS) {
        if (sscanf(line, "%31[^|]|%31[^|]|%31[^\n]", id, name, age) == 3) {
            strcpy(data->values[data->row_count][0], id);
            strcpy(data->values[data->row_count][1], name);
            strcpy(data->values[data->row_count][2], age);
            data->row_count++;
        }
    }

    fclose(file);
    return 1;
}

/* This appends one user row because INSERT only needs file storage. */
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

/* This prints known tables for the tiny CLI meta command. */
void list_tables(void) {
    FILE *file = fopen("users.schema", "r");

    if (file == NULL) {
        printf("no tables\n");
        return;
    }
    fclose(file);
    printf("users\n");
}

/* This prints schema because the CLI needs a quick table description view. */
int print_schema(const char *table_name) {
    TableSchema schema;
    int i;

    if (!load_schema(table_name, &schema)) {
        return 0;
    }
    for (i = 0; i < schema.column_count; i++) {
        printf("%s | %s\n", schema.names[i], schema.types[i]);
    }
    return 1;
}
