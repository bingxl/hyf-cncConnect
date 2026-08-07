#pragma once
#include <vector>
#include <atomic>
#include "core/StreamingData.hpp"
#include "core/CncData.hpp"

class HistoryVm {
public:
    // --- Save page ---
    StreamingData<HistoryEntry> save_stream;
    std::atomic<bool> saving{false};
    bool show_result = false;
    int saved_batch_id = 0;
    int saved_count = 0;
    void start_save();

    // --- Browse page ---
    std::vector<BatchInfoCpp> batch_list;
    int browse_page = 0;
    int total_batches = 0;
    static constexpr int page_size = 10;
    void load_batch_list();
    int total_pages() const;

    int selected_batch_id = -1;
    std::string selected_batch_time;
    std::vector<HistoryEntry> batch_entries;
    void load_batch_entries(int batch_id);

    void update_entry(int id, long required, long current, long total);
    void delete_entry(int id);
    void delete_batch(int batch_id);

    // --- Calc page ---
    StreamingData<CalcItem> calc_stream;
    std::vector<BatchInfoCpp> calc_batches;
    int selected_calc_start = -1;
    int selected_calc_end = -1;
    bool calc_end_is_live = true;
    void load_calc_batches();
    void compute_diff();
};
