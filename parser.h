#ifndef PARSER_H
#define PARSER_H

#include "common.h"

int parse_sql(const char *sql, Statement *stmt, char *error, size_t error_size);

#endif
