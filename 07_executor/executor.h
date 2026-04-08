#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "../04_common/common.h"

int execute_command(const Command *command, char *error_message, int error_size);

#endif
