#include <thread>
#include "HistoryVm.hpp"
#include "core/Database.hpp"
#include "core/CncConnection.hpp"

void HistoryVm::refresh_view() {
    auto machines = Database::instance().get_machines();
    view.start([machines = std::move(machines)]() {
        return parallel_fetch(machines, [](const MachineInfo& m) {
            HistoryEntry entry;
            entry.machine_id = m.id;
            entry.name = m.name;
            auto result = CncConnection::fetch(m.ip, m.port);
            if (result) {
                entry.current = result->part_count.current;
                entry.required = result->part_count.required;
                entry.total = result->part_count.total;
                entry.ok = result->ok;
                entry.alarm = result->status.alarm != 0;
            } else {
                entry.ok = false;
            }
            return entry;
        });
    });
}

void HistoryVm::save_batch() {
    bool expected = false;
    if (!saving.compare_exchange_strong(expected, true)) return;
    show_result = false;

    std::thread([this]() {
        auto machines = Database::instance().get_machines();
        auto results = parallel_fetch(machines, [](const MachineInfo& m) {
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
            return entry;
        });

        int batch_id = Database::instance().save_batch(results);
        saved_batch_id = batch_id;
        saved_count = static_cast<int>(results.size());
        show_result = true;
        saving = false;
    }).detach();
}

void HistoryVm::load_batches() {
    batches = Database::instance().get_batches();
    if (!batches.empty() && selected_batch < 0)
        selected_batch = 0;
}

void HistoryVm::compute_diff() {
    if (selected_batch < 0 || selected_batch >= static_cast<int>(batches.size())) return;

    int batch_id = batches[selected_batch].batch_id;
    auto machines = Database::instance().get_machines();
    auto hist = Database::instance().get_batch_history(batch_id);

    calc.start([machines = std::move(machines), hist = std::move(hist)]() {
        return parallel_fetch(machines, [&hist](const MachineInfo& m) {
            CalcItem item;
            item.name = m.name;
            long base_val = -1;

            for (auto& h : hist) {
                if (h.machine_id == m.id) {
                    base_val = h.total;
                    break;
                }
            }

            auto result = CncConnection::fetch(m.ip, m.port);
            if (result) {
                item.current = result->part_count.total;
                item.ok = result->ok;
            }

            if (base_val >= 0 && item.current >= 0) {
                item.diff = item.current - base_val;
                if (item.diff < 0) item.status = "异常: 减少";
                else if (item.diff == 0) item.status = "未变化";
                else item.status = "正常";
            } else if (item.current < 0) {
                item.diff = 0;
                item.status = "异常: 离线";
            } else {
                item.diff = 0;
                item.status = "异常: 无基准";
            }
            return item;
        });
    });
}
