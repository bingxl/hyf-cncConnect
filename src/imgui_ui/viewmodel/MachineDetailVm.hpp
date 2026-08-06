#pragma once
#include <string>
#include "core/AsyncData.hpp"

class MachineDetailVm {
public:
    AsyncData<MachineData> data;
    std::string current_name;
    int current_machine_id = 0;

    void fetch(int machine_id);
};
