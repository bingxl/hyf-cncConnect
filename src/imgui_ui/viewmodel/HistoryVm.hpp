#pragma once
#include <vector>
#include <atomic>
#include "core/AsyncData.hpp"
#include "core/CncData.hpp"

class HistoryVm {
public:
    AsyncData<std::vector<HistoryEntry>> view;
    void refresh_view();

    std::atomic<bool> saving{false};
    bool show_result = false;
    int saved_batch_id = 0;
    int saved_count = 0;
    void save_batch();

    AsyncData<std::vector<CalcItem>> calc;
    std::vector<BatchInfoCpp> batches;
    int selected_batch = -1;
    void load_batches();
    void compute_diff();
};
