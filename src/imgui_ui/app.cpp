#include <windows.h>
#include <filesystem>
#include "App.hpp"
#include "core/Database.hpp"

std::string App::get_db_path() {
    std::string home = std::getenv("USERPROFILE");
    if (home.empty()) home = ".";

    // All configuration, database and output files live together under
    // %USERPROFILE%\data-collect\ (single source of truth).
    namespace fs = std::filesystem;
    fs::path dir = fs::path(home) / "data-collect";
    std::error_code ec;
    if (!fs::exists(dir)) fs::create_directories(dir, ec);

    return (dir / "cnc_monitor.db").string();
}

bool App::init() {
    return Database::instance().open(get_db_path());
}

void App::cleanup() {
    Database::instance().close();
}
