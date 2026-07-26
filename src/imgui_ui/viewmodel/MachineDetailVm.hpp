#pragma once
#include <string>
#include "core/AsyncData.hpp"

class MachineDetailVm {
public:
    AsyncData<MachineData> data;
    std::string current_name;

    void fetch(int machine_id);
};
