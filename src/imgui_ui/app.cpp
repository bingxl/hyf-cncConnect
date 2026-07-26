#include <windows.h>
#include "App.hpp"
#include "core/Database.hpp"

std::string App::get_db_path() {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::string path(exe_path);
    auto pos = path.find_last_of("\\/");
    if (pos != std::string::npos)
        path = path.substr(0, pos + 1);
    path += "cnc_monitor.db";
    return path;
}

bool App::init() {
    return Database::instance().open(get_db_path());
}

void App::cleanup() {
    Database::instance().close();
}
