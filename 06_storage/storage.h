#ifndef STORAGE_H
#define STORAGE_H

int append_row_to_table(const char *table_name, const char values[][128], int value_count,
                        char *error_message, int error_size);
int print_table_rows(const char *table_name, char *error_message, int error_size);

#endif
