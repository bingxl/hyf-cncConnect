#include <windows.h>
#include <stdio.h>
#include <string>
#include "app.h"
#include "db_ops.h"

DbHandle g_db = NULL;

static std::string get_db_path()
{
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::string path(exe_path);
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos)
        path = path.substr(0, pos + 1);
    path += "cnc_monitor.db";
    return path;
}

int app_init(void)
{
    std::string db_path = get_db_path();
    g_db = db_open(db_path.c_str());
    if (!g_db) return -1;
    if (db_init_tables(g_db) != 0) return -1;
    return 0;
}

void app_cleanup(void)
{
    if (g_db) {
        db_close(g_db);
        g_db = NULL;
    }
}
