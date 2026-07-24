#ifndef APP_H
#define APP_H

#include "db_ops.h"

extern DbHandle g_db;

int app_init(void);
void app_cleanup(void);

#endif
