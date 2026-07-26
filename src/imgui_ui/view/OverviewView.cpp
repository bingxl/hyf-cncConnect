#include "imgui.h"
#include "OverviewView.hpp"

void overview_view_draw(OverviewVm& vm, int& selected_machine_id, UiPage& page) {
    ImGui::Text("机床总览");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("刷新数据", ImVec2(120, 0))) {
        if (!vm.data.is_loading()) {
            vm.refresh();
        }
    }
    ImGui::Spacing();

    if (vm.data.is_loading()) {
        ImGui::Text("正在获取各机床数据...");
        return;
    }

    if (!vm.data.is_loaded()) {
        ImGui::TextDisabled("请点击\"刷新数据\"按钮获取各机床当前加工数量");
        return;
    }

    auto guard = vm.data.lock();
    auto& items = vm.data.data();

    if (items.empty()) {
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

        for (auto& item : items) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            char selectable_label[128];
            snprintf(selectable_label, sizeof(selectable_label), "%s###%d",
                     item.name.c_str(), item.machine_id);
            bool row_clicked = ImGui::Selectable(selectable_label, false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            if (row_clicked) {
                selected_machine_id = item.machine_id;
                page = UiPage::MachineDetail;
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

    ImGui::TextDisabled("共 %d 台机床  |  点击行可查看详情", (int)items.size());
}
