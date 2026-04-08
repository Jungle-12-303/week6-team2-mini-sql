#ifndef DISPLAY_H
#define DISPLAY_H

#include "query.h"
#include "storage.h"

int print_select_result(const Query *query, const TableSchema *schema, const TableData *data);

#endif
