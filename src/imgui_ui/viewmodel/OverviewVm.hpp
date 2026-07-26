#pragma once
#include <vector>
#include "core/AsyncData.hpp"

class OverviewVm {
public:
    AsyncData<std::vector<OverviewItem>> data;
    void refresh();
};
