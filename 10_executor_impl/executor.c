#include <stdio.h>

#include "../06_storage/storage.h"
#include "../07_executor/executor.h"

int execute_command(const Command *command, char *error_message, int error_size) {
    if (command->type == COMMAND_INSERT) {
        if (!append_row_to_table(command->table_name, command->values, command->value_count,
                                 error_message, error_size)) {
            return 0;
        }

        printf("OK: 1 row inserted into %s\n", command->table_name);
        return 1;
    }

    if (command->type == COMMAND_SELECT) {
        return print_table_rows(command->table_name, error_message, error_size);
    }

    snprintf(error_message, error_size, "unknown command type");
    return 0;
}
