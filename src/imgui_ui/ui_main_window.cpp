#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstring>
#include "imgui.h"
#include "ui_main_window.h"
#include "ui_machine_mgr.h"
#include "ui_machine_detail.h"
#include "ui_history.h"
#include "cnc_ops.h"
#include "db_ops.h"

extern DbHandle g_db;

struct OverviewItem {
    int machine_id;
    char name[DB_MAX_NAME];
    long current;
    long required;
    long total;
    bool ok;
    bool alarm;
};

struct OverviewState {
    std::mutex mtx;
    std::vector<OverviewItem> items;
    std::atomic<bool> loading{false};
    std::atomic<bool> loaded{false};
};

static OverviewState s_overview;

static void fetch_overview_async(void)
{
    if (!g_db) return;
    s_overview.loading = true;
    s_overview.loaded = false;

    MachineRecord machines[DB_MAX_MACHINES];
    int count = db_get_machines(g_db, machines, DB_MAX_MACHINES);

    std::vector<OverviewItem> results(count);
    std::vector<std::thread> threads;

    for (int i = 0; i < count; i++) {
        threads.emplace_back([&, i]() {
            CncMachineData data;
            fetch_machine_data(machines[i].ip, machines[i].port, &data);
            results[i].machine_id = machines[i].id;
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
        std::lock_guard<std::mutex> lock(s_overview.mtx);
        s_overview.items = std::move(results);
    }
    s_overview.loaded = true;
    s_overview.loading = false;
}

static void draw_overview(int &selected_machine_id, UiPage &current_page)
{
    ImGui::Text("机床总览");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("刷新数据", ImVec2(120, 0))) {
        if (!s_overview.loading) {
            std::thread t(fetch_overview_async);
            t.detach();
        }
    }
    ImGui::Spacing();

    if (s_overview.loading) {
        ImGui::Text("正在获取各机床数据...");
        return;
    }

    if (!s_overview.loaded) {
        ImGui::TextDisabled("请点击\"刷新数据\"按钮获取各机床当前加工数量");
        return;
    }

    std::lock_guard<std::mutex> lock(s_overview.mtx);
    if (s_overview.items.empty()) {
        ImGui::TextDisabled("暂无机床数据，请先在\"机床管理\"中添加机床");
        return;
    }

    if (ImGui::BeginTable("##overview", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("机床名称", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("当前数量", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("要求数量", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("累计数量", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();

        for (auto &item : s_overview.items) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            char selectable_label[128];
            snprintf(selectable_label, sizeof(selectable_label), "%s###%d", item.name, item.machine_id);
            bool row_clicked = ImGui::Selectable(selectable_label, false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            if (row_clicked) {
                selected_machine_id = item.machine_id;
                current_page = UiPage::MachineDetail;
            }

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

    ImGui::TextDisabled("共 %d 台机床  |  点击行可查看详情", (int)s_overview.items.size());
}

void ui_main_window_draw(UiPage &current_page, int &selected_machine_id)
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##MainWindow", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    float sidebar_w = 170.0f;
    ImGui::BeginChild("##Sidebar", ImVec2(sidebar_w, 0), true);
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "CNC 监控系统");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        auto nav_button = [&](const char *label, UiPage page) {
            bool active = (current_page == page);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 0.9f));
            }
            if (ImGui::Button(label, ImVec2(-1, 32)))
                current_page = page;
            if (active)
                ImGui::PopStyleColor(2);
        };

        nav_button("机床总览", UiPage::Overview);
        nav_button("机床管理", UiPage::MachineMgr);
        nav_button("历史查看", UiPage::HistoryView);
        nav_button("保存批次", UiPage::HistorySave);
        nav_button("差值计算", UiPage::HistoryCalc);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (selected_machine_id > 0) {
            ImGui::TextDisabled("当前选中");
            MachineRecord rec;
            if (db_get_machine_by_id(g_db, selected_machine_id, &rec) == 0) {
                ImGui::TextWrapped("%s", rec.name);
                if (ImGui::SmallButton("查看详情"))
                    current_page = UiPage::MachineDetail;
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##Content", ImVec2(0, 0), true);
    {
        switch (current_page) {
        case UiPage::Overview:
            draw_overview(selected_machine_id, current_page);
            break;
        case UiPage::MachineMgr:
            ui_machine_mgr_draw();
            break;
        case UiPage::MachineDetail:
            ui_machine_detail_draw(selected_machine_id);
            break;
        case UiPage::HistoryView:
            ui_history_view_draw();
            break;
        case UiPage::HistorySave:
            ui_history_save_draw();
            break;
        case UiPage::HistoryCalc:
            ui_history_calc_draw();
            break;
        }
    }
    ImGui::EndChild();

    ImGui::End();
}
