#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstring>
#include "imgui.h"
#include "ui_history.h"
#include "cnc_ops.h"
#include "db_ops.h"

extern DbHandle g_db;

struct HistoryItem {
    char name[DB_MAX_NAME];
    long current;
    long required;
    long total;
    bool ok;
    bool alarm;
};

struct HistoryViewState {
    std::mutex mtx;
    std::vector<HistoryItem> items;
    std::atomic<bool> loading{false};
    std::atomic<bool> loaded{false};
};

static HistoryViewState s_hist_view;

static void fetch_all_machines_async(void)
{
    if (!g_db) return;
    s_hist_view.loading = true;
    s_hist_view.loaded = false;

    MachineRecord machines[DB_MAX_MACHINES];
    int count = db_get_machines(g_db, machines, DB_MAX_MACHINES);

    std::vector<HistoryItem> results(count);
    std::vector<std::thread> threads;

    for (int i = 0; i < count; i++) {
        threads.emplace_back([&, i]() {
            CncMachineData data;
            fetch_machine_data(machines[i].ip, machines[i].port, &data);
            strncpy(results[i].name, machines[i].name, DB_MAX_NAME - 1);
            results[i].current = data.part_count.current;
            results[i].required = data.part_count.required;
            results[i].total = data.part_count.total;
            results[i].ok = data.ok;
            results[i].alarm = data.status.alarm;
        });
    }
    for (auto &t : threads) t.join();

    {
        std::lock_guard<std::mutex> lock(s_hist_view.mtx);
        s_hist_view.items = std::move(results);
    }
    s_hist_view.loaded = true;
    s_hist_view.loading = false;
}

void ui_history_view_draw(void)
{
    ImGui::Text("历史加工数量");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("获取数据", ImVec2(120, 0))) {
        if (!s_hist_view.loading) {
            std::thread t(fetch_all_machines_async);
            t.detach();
        }
    }
    ImGui::Spacing();

    if (s_hist_view.loading) {
        ImGui::Text("正在多线程获取各机床数据...");
        return;
    }

    if (!s_hist_view.loaded) {
        ImGui::TextDisabled("请点击\"获取数据\"按钮查看各机床当前历史加工数量");
        return;
    }

    std::lock_guard<std::mutex> lock(s_hist_view.mtx);
    if (s_hist_view.items.empty()) {
        ImGui::TextDisabled("暂无机床数据");
        return;
    }

    if (ImGui::BeginTable("##history", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("机床名称", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("当前数量", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("要求数量", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("累计数量", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();

        for (auto &item : s_hist_view.items) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(item.name);
            ImGui::TableSetColumnIndex(1);
            if (item.ok)
                ImGui::Text("%ld", item.current);
            else
                ImGui::TextDisabled("--");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%ld", item.required);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%ld", item.total);
            ImGui::TableSetColumnIndex(4);
            if (!item.ok)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "连接失败");
            else if (item.alarm)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "报警");
            else
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "正常");
        }
        ImGui::EndTable();
    }

    ImGui::TextDisabled("共 %d 台机床", (int)s_hist_view.items.size());
}

struct SaveState {
    std::mutex mtx;
    std::atomic<bool> saving{false};
    bool show_result = false;
    int saved_batch_id = 0;
    int saved_count = 0;
};

static SaveState s_save;

static void save_batch_async(void)
{
    if (!g_db) return;
    s_save.saving = true;
    s_save.show_result = false;

    MachineRecord machines[DB_MAX_MACHINES];
    HistoryRecord records[DB_MAX_MACHINES];
    int count = db_get_machines(g_db, machines, DB_MAX_MACHINES);

    std::vector<std::thread> threads;
    for (int i = 0; i < count; i++) {
        threads.emplace_back([&, i]() {
            CncMachineData data;
            fetch_machine_data(machines[i].ip, machines[i].port, &data);
            records[i].machine_id = machines[i].id;
            records[i].required = data.part_count.required;
            records[i].current = data.part_count.current;
            records[i].total = data.part_count.total;
        });
    }
    for (auto &t : threads) t.join();

    int batch_id = -1;
    if (count > 0)
        batch_id = db_save_batch(g_db, records, count);

    {
        std::lock_guard<std::mutex> lock(s_save.mtx);
        s_save.saved_batch_id = batch_id;
        s_save.saved_count = count;
        s_save.show_result = true;
    }
    s_save.saving = false;
}

void ui_history_save_draw(void)
{
    ImGui::Text("保存数据");
    ImGui::Separator();
    ImGui::TextWrapped("将当前所有机床的历史加工数量与机床名和时间保存到数据库中。");
    ImGui::Spacing();

    bool can_save = g_db && !s_save.saving;
    if (!can_save) ImGui::BeginDisabled();
    if (ImGui::Button("保存当前批次", ImVec2(160, 0))) {
        std::thread t(save_batch_async);
        t.detach();
    }
    if (!can_save) ImGui::EndDisabled();

    ImGui::Spacing();

    if (s_save.saving) {
        ImGui::Text("正在多线程获取各机床数据并保存...");
        ImGui::ProgressBar(-1.0f, ImVec2(-1, 0), "保存中...");
    }

    if (s_save.show_result) {
        std::lock_guard<std::mutex> lock(s_save.mtx);
        if (s_save.saved_batch_id > 0) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f),
                "保存成功!");
            ImGui::Text("批次号: %d", s_save.saved_batch_id);
            ImGui::Text("机床数量: %d", s_save.saved_count);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "保存失败");
        }
    }
}

struct CalcBatch {
    int batch_id;
    char save_time[DB_MAX_TIME];
};

struct CalcItem {
    char name[DB_MAX_NAME];
    long current;
    long base;
    long diff;
    const char *status;
};

struct HistoryCalcState {
    std::mutex mtx;
    std::vector<CalcBatch> batches;
    std::vector<CalcItem> items;
    int selected_batch = -1;
    bool computed = false;
    std::atomic<bool> computing{false};
};

static HistoryCalcState s_calc;

static void compute_diff_async(int batch_id, HistoryCalcState *state)
{
    state->computing = true;
    state->computed = false;

    HistoryRecord hist[DB_MAX_MACHINES];
    MachineRecord machines[DB_MAX_MACHINES];
    int mcount = db_get_machines(g_db, machines, DB_MAX_MACHINES);
    int hcount = db_get_batch_history(g_db, batch_id, hist, DB_MAX_MACHINES);

    std::vector<CalcItem> items(mcount);
    std::vector<std::thread> threads;

    for (int j = 0; j < mcount; j++) {
        threads.emplace_back([&, j]() {
            CncMachineData data;
            long base_val = -1, cur_val = -1;

            strncpy(items[j].name, machines[j].name, DB_MAX_NAME - 1);

            for (int k = 0; k < hcount; k++) {
                if (hist[k].machine_id == machines[j].id) {
                    base_val = hist[k].total;
                    break;
                }
            }

            fetch_machine_data(machines[j].ip, machines[j].port, &data);
            if (data.ok) cur_val = data.part_count.total;

            items[j].current = cur_val;
            items[j].base = base_val;

            if (base_val >= 0 && cur_val >= 0) {
                items[j].diff = cur_val - base_val;
                if (items[j].diff < 0)
                    items[j].status = "异常: 减少";
                else if (items[j].diff == 0)
                    items[j].status = "未变化";
                else
                    items[j].status = "正常";
            } else if (cur_val < 0) {
                items[j].diff = 0;
                items[j].status = "异常: 离线";
            } else {
                items[j].diff = 0;
                items[j].status = "异常: 无基准";
            }
        });
    }
    for (auto &t : threads) t.join();

    {
        std::lock_guard<std::mutex> lock(state->mtx);
        state->items = std::move(items);
    }
    state->computed = true;
    state->computing = false;
}

void ui_history_calc_draw(void)
{
    ImGui::Text("历史数据计算");
    ImGui::Separator();
    ImGui::Spacing();

    if (!g_db) {
        ImGui::TextDisabled("数据库未初始化");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(s_calc.mtx);
        if (s_calc.batches.empty()) {
            BatchInfo batches[4];
            int count = db_get_batch_list(g_db, batches, 4);
            s_calc.batches.resize(count);
            for (int i = 0; i < count; i++) {
                s_calc.batches[i].batch_id = batches[i].batch_id;
                strncpy(s_calc.batches[i].save_time, batches[i].save_time, DB_MAX_TIME - 1);
            }
            if (count > 0) s_calc.selected_batch = 0;
        }
    }

    std::lock_guard<std::mutex> lock(s_calc.mtx);

    if (s_calc.batches.empty()) {
        ImGui::TextDisabled("没有历史数据，请先在\"保存批次\"中保存数据");
        return;
    }

    ImGui::Text("基准批次 (最近4批次):");
    ImGui::Spacing();

    if (ImGui::BeginCombo("##batch",
            s_calc.selected_batch >= 0 ? s_calc.batches[s_calc.selected_batch].save_time : "选择批次")) {
        for (int i = 0; i < (int)s_calc.batches.size(); i++) {
            bool selected = (i == s_calc.selected_batch);
            if (ImGui::Selectable(s_calc.batches[i].save_time, selected)) {
                s_calc.selected_batch = i;
                s_calc.computed = false;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    bool can_calc = s_calc.selected_batch >= 0 && !s_calc.computing;
    if (!can_calc) ImGui::BeginDisabled();
    if (ImGui::Button("计算差值")) {
        int batch_id = s_calc.batches[s_calc.selected_batch].batch_id;
        std::thread t(compute_diff_async, batch_id, &s_calc);
        t.detach();
    }
    if (!can_calc) ImGui::EndDisabled();

    ImGui::Spacing();

    if (s_calc.computing) {
        ImGui::Text("正在多线程计算各机床差值...");
        ImGui::ProgressBar(-1.0f, ImVec2(-1, 0), "计算中...");
        return;
    }

    if (s_calc.computed && !s_calc.items.empty()) {
        if (ImGui::BeginTable("##calc", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("机床名称", ImGuiTableColumnFlags_WidthFixed, 140);
            ImGui::TableSetupColumn("当前数量", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("基准数量", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("差值", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableHeadersRow();

            for (auto &item : s_calc.items) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(item.name);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%ld", item.current);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%ld", item.base);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%+ld", item.diff);
                ImGui::TableSetColumnIndex(4);

                ImVec4 clr(0.9f, 0.9f, 0.9f, 1.0f);
                if (strstr(item.status, "异常"))
                    clr = ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
                else if (strcmp(item.status, "正常") == 0)
                    clr = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
                else
                    clr = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                ImGui::TextColored(clr, "%s", item.status);
            }
            ImGui::EndTable();
        }
    } else if (!s_calc.computed) {
        ImGui::TextDisabled("选择基准批次后点击\"计算差值\"");
    }
}
