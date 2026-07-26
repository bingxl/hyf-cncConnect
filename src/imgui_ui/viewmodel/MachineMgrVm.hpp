#pragma once
#include <vector>
#include "core/CncData.hpp"

class MachineMgrVm {
public:
    std::vector<MachineInfo> machines;

    void load();
    bool add(std::string_view name, std::string_view ip, int port);
    bool update(int id, std::string_view name, std::string_view ip, int port);
    bool remove(int id);
};
