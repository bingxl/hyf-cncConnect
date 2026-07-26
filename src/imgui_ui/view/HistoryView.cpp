#include <cstring>
#include "imgui.h"
#include "HistoryView.hpp"

void history_view_draw(HistoryVm& vm) {
    ImGui::Text("历史加工数量");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("获取数据", ImVec2(120, 0))) {
        if (!vm.view.is_loading()) {
            vm.refresh_view();
        }
    }
    ImGui::Spacing();

    if (vm.view.is_loading()) {
        ImGui::Text("正在多线程获取各机床数据...");
        return;
    }

    if (!vm.view.is_loaded()) {
        ImGui::TextDisabled("请点击\"获取数据\"按钮查看各机床当前历史加工数量");
        return;
    }

    auto guard = vm.view.lock();
    auto& items = vm.view.data();

    if (items.empty()) {
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

        for (auto& item : items) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(item.name.c_str());
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

    ImGui::TextDisabled("共 %d 台机床", (int)items.size());
}

void history_save_draw(HistoryVm& vm) {
    ImGui::Text("保存数据");
    ImGui::Separator();
    ImGui::TextWrapped("将当前所有机床的历史加工数量与机床名和时间保存到数据库中。");
    ImGui::Spacing();

    bool can_save = !vm.saving.load();
    if (!can_save) ImGui::BeginDisabled();
    if (ImGui::Button("保存当前批次", ImVec2(160, 0))) {
        vm.save_batch();
    }
    if (!can_save) ImGui::EndDisabled();

    ImGui::Spacing();

    if (vm.saving.load()) {
        ImGui::Text("正在多线程获取各机床数据并保存...");
        ImGui::ProgressBar(-1.0f, ImVec2(-1, 0), "保存中...");
    }

    if (vm.show_result) {
        if (vm.saved_batch_id > 0) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "保存成功!");
            ImGui::Text("批次号: %d", vm.saved_batch_id);
            ImGui::Text("机床数量: %d", vm.saved_count);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "保存失败");
        }
    }
}

void history_calc_draw(HistoryVm& vm) {
    ImGui::Text("历史数据计算");
    ImGui::Separator();
    ImGui::Spacing();

    if (vm.batches.empty()) {
        vm.load_batches();
    }

    if (vm.batches.empty()) {
        ImGui::TextDisabled("没有历史数据，请先在\"保存批次\"中保存数据");
        return;
    }

    ImGui::Text("基准批次 (最近4批次):");
    ImGui::Spacing();

    if (ImGui::BeginCombo("##batch",
            vm.selected_batch >= 0
                ? vm.batches[vm.selected_batch].save_time.c_str()
                : "选择批次")) {
        for (int i = 0; i < (int)vm.batches.size(); i++) {
            bool selected = (i == vm.selected_batch);
            if (ImGui::Selectable(vm.batches[i].save_time.c_str(), selected)) {
                vm.selected_batch = i;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    bool can_calc = vm.selected_batch >= 0 && !vm.calc.is_loading();
    if (!can_calc) ImGui::BeginDisabled();
    if (ImGui::Button("计算差值")) {
        vm.compute_diff();
    }
    if (!can_calc) ImGui::EndDisabled();

    ImGui::Spacing();

    if (vm.calc.is_loading()) {
        ImGui::Text("正在多线程计算各机床差值...");
        ImGui::ProgressBar(-1.0f, ImVec2(-1, 0), "计算中...");
        return;
    }

    if (vm.calc.is_loaded()) {
        auto guard = vm.calc.lock();
        auto& items = vm.calc.data();

        if (!items.empty()) {
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

                for (auto& item : items) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(item.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%ld", item.current);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%ld", item.base);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%+ld", item.diff);
                    ImGui::TableSetColumnIndex(4);

                    ImVec4 clr(0.9f, 0.9f, 0.9f, 1.0f);
                    if (item.status.find("异常") != std::string::npos)
                        clr = ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
                    else if (item.status == "正常")
                        clr = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
                    else
                        clr = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                    ImGui::TextColored(clr, "%s", item.status.c_str());
                }
                ImGui::EndTable();
            }
        }
    } else {
        ImGui::TextDisabled("选择基准批次后点击\"计算差值\"");
    }
}
