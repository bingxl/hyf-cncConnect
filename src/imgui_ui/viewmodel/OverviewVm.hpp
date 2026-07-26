#pragma once
#include "core/StreamingData.hpp"

class OverviewVm {
public:
    StreamingData<OverviewItem> data;
    void refresh();
};
