#include <thread>
#include <algorithm>
#include <unordered_map>
#include "HistoryVm.hpp"
#include "core/Database.hpp"
#include "core/CncConnection.hpp"

static void sort_by_name(std::vector<MachineInfo>& machines) {
    std::sort(machines.begin(), machines.end(),
              [](const MachineInfo& a, const MachineInfo& b) {
                  return a.name < b.name;
              });
}

// --- Save page ---

void HistoryVm::start_save() {
    bool expected = false;
    if (!saving.compare_exchange_strong(expected, true)) return;
    show_result = false;

    auto machines = Database::instance().get_machines();
    if (machines.empty()) {
        saved_batch_id = -1;
        saved_count = 0;
        show_result = true;
        saving = false;
        return;
    }
    sort_by_name(machines);

    std::vector<HistoryEntry> placeholders;
    placeholders.reserve(machines.size());
    for (const auto& m : machines) {
        HistoryEntry e;
        e.machine_id = m.id;
        e.name = m.name;
        e.loading = true;
        placeholders.push_back(std::move(e));
    }

    streaming_fetch_update(machines, save_stream, std::move(placeholders),
        [this](const MachineInfo& m) {
            HistoryEntry entry;
            entry.machine_id = m.id;
            entry.name = m.name;
            auto result = CncConnection::fetch(m.ip, m.port);
            if (result) {
                entry.required = result->part_count.required;
                entry.current = result->part_count.current;
                entry.total = result->part_count.total;
                entry.ok = true;
            }
            entry.loading = false;
            return entry;
        });

    std::thread([this]() {
        while (save_stream.is_loading())
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::vector<HistoryEntry> results;
        {
            auto guard = save_stream.lock();
            results = save_stream.items();
        }

        int batch_id = Database::instance().save_batch(results);
        saved_batch_id = batch_id;
        saved_count = static_cast<int>(results.size());
        show_result = true;
        saving = false;
    }).detach();
}

// --- Browse page ---

void HistoryVm::load_batch_list() {
    total_batches = Database::instance().batch_count();
    batch_list = Database::instance().get_batches_paged(browse_page, page_size);
}

int HistoryVm::total_pages() const {
    if (total_batches <= 0) return 0;
    return (total_batches + page_size - 1) / page_size;
}

void HistoryVm::load_batch_entries(int batch_id) {
    selected_batch_id = batch_id;
    batch_entries = Database::instance().get_batch_history(batch_id);

    for (auto& b : batch_list) {
        if (b.batch_id == batch_id) {
            selected_batch_time = b.save_time;
            break;
        }
    }
}

void HistoryVm::update_entry(int id, long required, long current, long total) {
    Database::instance().update_history_entry(id, required, current, total);
    if (selected_batch_id >= 0)
        load_batch_entries(selected_batch_id);
}

void HistoryVm::delete_entry(int id) {
    Database::instance().delete_history_entry(id);
    if (selected_batch_id >= 0)
        load_batch_entries(selected_batch_id);
}

void HistoryVm::delete_batch(int batch_id) {
    Database::instance().delete_batch(batch_id);
    if (selected_batch_id == batch_id) {
        selected_batch_id = -1;
        batch_entries.clear();
        selected_batch_time.clear();
    }
    load_batch_list();
}

// --- Calc page ---

void HistoryVm::load_calc_batches() {
    calc_batches = Database::instance().get_batches(20);
    if (calc_batches.empty()) {
        selected_calc_start = -1;
        selected_calc_end = -1;
        return;
    }
    if (selected_calc_start < 0 || selected_calc_start >= static_cast<int>(calc_batches.size()))
        selected_calc_start = 0;
    if (selected_calc_end < 0 || selected_calc_end >= static_cast<int>(calc_batches.size()))
        selected_calc_end = static_cast<int>(calc_batches.size()) - 1;
}

void HistoryVm::compute_diff() {
    if (selected_calc_start < 0 || selected_calc_start >= static_cast<int>(calc_batches.size()))
        return;
    if (!calc_end_is_live &&
        (selected_calc_end < 0 || selected_calc_end >= static_cast<int>(calc_batches.size())))
        return;

    int start_id = calc_batches[selected_calc_start].batch_id;
    auto machines = Database::instance().get_machines();
    auto start_hist = Database::instance().get_batch_history(start_id);
    sort_by_name(machines);

    std::unordered_map<int, long> start_map;
    for (const auto& h : start_hist)
        start_map[h.machine_id] = h.total;

    std::unordered_map<int, long> end_map;
    if (!calc_end_is_live) {
        int end_id = calc_batches[selected_calc_end].batch_id;
        auto end_hist = Database::instance().get_batch_history(end_id);
        for (const auto& h : end_hist)
            end_map[h.machine_id] = h.total;
    }

    std::vector<CalcItem> placeholders;
    placeholders.reserve(machines.size());
    for (const auto& m : machines) {
        CalcItem item;
        item.name = m.name;
        item.loading = true;
        placeholders.push_back(std::move(item));
    }

    streaming_fetch_update(machines, calc_stream, std::move(placeholders),
        [use_live_end = calc_end_is_live, start_map = std::move(start_map),
         end_map = std::move(end_map)](const MachineInfo& m) {
        CalcItem item;
        item.name = m.name;
        item.base = -1;

        auto sit = start_map.find(m.id);
        if (sit != start_map.end())
            item.base = sit->second;

        if (use_live_end) {
            auto result = CncConnection::fetch(m.ip, m.port);
            if (result) {
                item.current = result->part_count.total;
                item.ok = result->ok;
            }
        } else {
            auto eit = end_map.find(m.id);
            if (eit != end_map.end())
                item.current = eit->second;
        }

        if (item.base >= 0 && item.current >= 0) {
            item.diff = item.current - item.base;
            if (item.diff < 0) item.status = "异常: 减少";
            else if (item.diff == 0) item.status = "未变化";
            else item.status = "正常";
        } else if (item.base < 0) {
            item.diff = 0;
            item.status = "异常: 无起始数据";
        } else {
            item.diff = 0;
            item.status = use_live_end ? "异常: 离线" : "异常: 无结束数据";
        }
        item.loading = false;
        return item;
    });
}
