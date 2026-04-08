#ifndef PARSER_H
#define PARSER_H

#include "../04_common/common.h"

void init_command(Command *command);
void free_command(Command *command);
int parse_sql(const char *sql, Command *command, char *error_message, int error_size);

#endif
