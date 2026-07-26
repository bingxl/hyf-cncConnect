#include "imgui.h"
#include "OverviewView.hpp"

void OverviewPage::draw(AppState& state) {
    ImGui::Text("机床总览");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("刷新数据", ImVec2(120, 0))) {
        if (!vm_.data.is_loading()) {
            vm_.refresh();
        }
    }
    ImGui::Spacing();

    if (vm_.data.is_loading()) {
        ImGui::Text("正在获取数据... (%d/%d)", vm_.data.count(), vm_.data.total());
    }

    if (vm_.data.count() == 0) {
        if (!vm_.data.is_loading())
            ImGui::TextDisabled("请点击\"刷新数据\"按钮获取各机床当前加工数量");
        return;
    }

    auto guard = vm_.data.lock();
    auto& items = vm_.data.items();

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
                state.select_machine(item.machine_id, item.name);
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
