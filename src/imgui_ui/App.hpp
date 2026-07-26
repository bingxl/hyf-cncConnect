#pragma once
#include <string>

class App {
public:
    static bool init();
    static void cleanup();

private:
    static std::string get_db_path();
};
