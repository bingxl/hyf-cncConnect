#include "imgui.h"
#include "MainView.hpp"
#include "core/CncData.hpp"
#include "core/Database.hpp"
#include "OverviewView.hpp"
#include "MachineMgrView.hpp"
#include "MachineDetailView.hpp"
#include "HistoryView.hpp"

void MainView::draw_sidebar(UiPage& page, int& selected_machine_id) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "CNC 监控系统");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    auto nav_button = [&](const char* label, UiPage target) {
        bool active = (page == target);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 0.9f));
        }
        if (ImGui::Button(label, ImVec2(-1, 32)))
            page = target;
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
        auto rec = Database::instance().get_machine(selected_machine_id);
        if (rec) {
            ImGui::TextWrapped("%s", rec->name.c_str());
            if (ImGui::SmallButton("查看详情"))
                page = UiPage::MachineDetail;
        }
    }
}

void MainView::draw(UiPage& page, int& selected_machine_id) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##MainWindow", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    float sidebar_w = 170.0f;
    ImGui::BeginChild("##Sidebar", ImVec2(sidebar_w, 0), true);
    draw_sidebar(page, selected_machine_id);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##Content", ImVec2(0, 0), true);
    {
        switch (page) {
        case UiPage::Overview:
            overview_view_draw(overview_vm_, selected_machine_id, page);
            break;
        case UiPage::MachineMgr:
            machine_mgr_view_draw(machine_mgr_vm_);
            break;
        case UiPage::MachineDetail:
            machine_detail_view_draw(machine_detail_vm_, selected_machine_id);
            break;
        case UiPage::HistoryView:
            history_view_draw(history_vm_);
            break;
        case UiPage::HistorySave:
            history_save_draw(history_vm_);
            break;
        case UiPage::HistoryCalc:
            history_calc_draw(history_vm_);
            break;
        }
    }
    ImGui::EndChild();

    ImGui::End();
}
