#include <cstring>
#include "imgui.h"
#include "HistoryView.hpp"

// ============================================================
// HistorySavePage
// ============================================================

void HistorySavePage::draw(AppState& state) {
    ImGui::Text("保存批次");
    ImGui::Separator();
    ImGui::TextWrapped("从所有机床获取当前数据并保存为一个批次。");
    ImGui::Spacing();

    bool can_save = !vm_.saving.load();
    if (!can_save) ImGui::BeginDisabled();
    if (ImGui::Button("获取并保存", ImVec2(160, 0))) {
        vm_.start_save();
    }
    if (!can_save) ImGui::EndDisabled();

    ImGui::Spacing();

    if (vm_.saving.load()) {
        ImGui::Text("正在获取数据... (%d/%d)", vm_.save_stream.completed(), vm_.save_stream.total());

        if (vm_.save_stream.count() > 0) {
            auto guard = vm_.save_stream.lock();
            auto& items = vm_.save_stream.items();

            if (ImGui::BeginTable("##save_preview", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("机床名称", ImGuiTableColumnFlags_WidthFixed, 140);
                ImGui::TableSetupColumn("当前数量", ImGuiTableColumnFlags_WidthFixed, 90);
                ImGui::TableSetupColumn("要求数量", ImGuiTableColumnFlags_WidthFixed, 90);
                ImGui::TableSetupColumn("累计数量", ImGuiTableColumnFlags_WidthFixed, 90);
                ImGui::TableHeadersRow();

                for (auto& item : items) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(item.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    if (!item.loading)
                        ImGui::Text("%ld", item.current);
                    else
                        ImGui::TextDisabled("--");
                    ImGui::TableSetColumnIndex(2);
                    if (!item.loading)
                        ImGui::Text("%ld", item.required);
                    else
                        ImGui::TextDisabled("--");
                    ImGui::TableSetColumnIndex(3);
                    if (!item.loading)
                        ImGui::Text("%ld", item.total);
                    else
                        ImGui::TextDisabled("--");
                }
                ImGui::EndTable();
            }
        }
    }

    if (vm_.show_result) {
        if (vm_.saved_batch_id == -1) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "没有机床数据，无法保存");
        } else if (vm_.saved_batch_id > 0) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "保存成功!");
            ImGui::Text("批次号: %d  |  机床数量: %d", vm_.saved_batch_id, vm_.saved_count);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "保存失败");
        }
    }
}

// ============================================================
// HistoryBrowsePage
// ============================================================

void HistoryBrowsePage::draw_batch_list() {
    if (vm_.batch_list.empty() && vm_.total_batches == 0)
        vm_.load_batch_list();

    int total_pages = vm_.total_pages();
    if (total_pages < 1) total_pages = 1;

    ImGui::Text("批次列表");
    ImGui::TextDisabled("共 %d 个批次", vm_.total_batches);
    ImGui::Spacing();

    if (ImGui::BeginTable("##batch_list", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("批次号", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("保存时间", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();

        for (auto& b : vm_.batch_list) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool selected = (b.batch_id == vm_.selected_batch_id);
            if (ImGui::Selectable(std::to_string(b.batch_id).c_str(), selected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                vm_.load_batch_entries(b.batch_id);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(b.save_time.c_str());
            ImGui::TableSetColumnIndex(2);
            char del_btn[32];
            snprintf(del_btn, sizeof(del_btn), "X##%d", b.batch_id);
            if (ImGui::SmallButton(del_btn)) {
                del_.show = true;
                del_.batch_id = b.batch_id;
                del_.is_batch = true;
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::PushID("pagination");
    if (ImGui::Button("上一页") && vm_.browse_page > 0) {
        vm_.browse_page--;
        vm_.load_batch_list();
    }
    ImGui::SameLine();
    ImGui::Text("第 %d / %d 页", vm_.browse_page + 1, total_pages);
    ImGui::SameLine();
    bool can_next = vm_.browse_page < total_pages - 1;
    if (!can_next) ImGui::BeginDisabled();
    if (ImGui::Button("下一页")) {
        vm_.browse_page++;
        vm_.load_batch_list();
    }
    if (!can_next) ImGui::EndDisabled();
    ImGui::PopID();
}

void HistoryBrowsePage::draw_batch_detail() {
    if (vm_.selected_batch_id < 0) {
        ImGui::TextDisabled("请选择一个批次");
        return;
    }

    ImGui::Text("批次 #%d  %s", vm_.selected_batch_id, vm_.selected_batch_time.c_str());
    ImGui::TextDisabled("%d 条记录", (int)vm_.batch_entries.size());
    ImGui::Spacing();

    if (vm_.batch_entries.empty()) {
        ImGui::TextDisabled("该批次无数据");
        return;
    }

    if (ImGui::BeginTable("##entries", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("机床名称", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("当前数量", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("要求数量", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("累计数量", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();

        for (auto& e : vm_.batch_entries) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%ld", e.current);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%ld", e.required);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%ld", e.total);
            ImGui::TableSetColumnIndex(4);
            char edit_btn[32];
            snprintf(edit_btn, sizeof(edit_btn), "编辑##%d", e.id);
            if (ImGui::SmallButton(edit_btn)) {
                edit_.show = true;
                edit_.entry_id = e.id;
                strncpy(edit_.name, e.name.c_str(), sizeof(edit_.name) - 1);
                edit_.required = e.required;
                edit_.current = e.current;
                edit_.total = e.total;
            }
            ImGui::SameLine();
            snprintf(edit_btn, sizeof(edit_btn), "删##%d", e.id);
            if (ImGui::SmallButton(edit_btn)) {
                del_.show = true;
                del_.entry_id = e.id;
                del_.is_batch = false;
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("删除整个批次")) {
        del_.show = true;
        del_.batch_id = vm_.selected_batch_id;
        del_.is_batch = true;
    }
    ImGui::PopStyleColor();
}

void HistoryBrowsePage::draw_edit_popup() {
    if (!edit_.show) return;

    ImGui::OpenPopup("编辑批次数据");
    if (ImGui::BeginPopupModal("编辑批次数据", &edit_.show,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("机床: %s", edit_.name);
        ImGui::Spacing();
        ImGui::InputInt("当前数量", &edit_.current);
        ImGui::InputInt("要求数量", &edit_.required);
        ImGui::InputInt("累计数量", &edit_.total);

        ImGui::Spacing();
        if (ImGui::Button("确定", ImVec2(120, 0))) {
            vm_.update_entry(edit_.entry_id, edit_.required, edit_.current, edit_.total);
            edit_.show = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) {
            edit_.show = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void HistoryBrowsePage::draw_delete_confirm() {
    if (!del_.show) return;

    ImGui::OpenPopup("确认删除");
    if (ImGui::BeginPopupModal("确认删除", &del_.show,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        if (del_.is_batch) {
            ImGui::Text("确定要删除批次 #%d 吗？", del_.batch_id);
            ImGui::TextDisabled("此操作不可恢复，将删除该批次的所有数据。");
        } else {
            ImGui::Text("确定要删除该条记录吗？");
        }
        ImGui::Spacing();

        if (ImGui::Button("删除", ImVec2(120, 0))) {
            if (del_.is_batch)
                vm_.delete_batch(del_.batch_id);
            else
                vm_.delete_entry(del_.entry_id);
            del_.show = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) {
            del_.show = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void HistoryBrowsePage::draw(AppState& state) {
    ImGui::Text("批次浏览");
    ImGui::Separator();
    ImGui::Spacing();

    float w = ImGui::GetContentRegionAvail().x;
    float list_w = w * 0.35f;

    ImGui::BeginChild("##batch_list", ImVec2(list_w, 0), true);
    draw_batch_list();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##batch_detail", ImVec2(0, 0), true);
    draw_batch_detail();
    ImGui::EndChild();

    draw_edit_popup();
    draw_delete_confirm();
}

// ============================================================
// HistoryCalcPage
// ============================================================

void HistoryCalcPage::draw(AppState& state) {
    ImGui::Text("差值计算");
    ImGui::Separator();
    ImGui::Spacing();

    vm_.load_calc_batches();

    if (vm_.calc_batches.empty()) {
        ImGui::TextDisabled("没有历史数据，请先在\"保存批次\"中保存数据");
        return;
    }

    ImGui::Text("基准批次:");
    ImGui::Spacing();

    if (ImGui::BeginCombo("##batch",
            vm_.selected_calc_batch >= 0
                ? vm_.calc_batches[vm_.selected_calc_batch].save_time.c_str()
                : "选择批次")) {
        for (int i = 0; i < (int)vm_.calc_batches.size(); i++) {
            bool selected = (i == vm_.selected_calc_batch);
            if (ImGui::Selectable(vm_.calc_batches[i].save_time.c_str(), selected)) {
                vm_.selected_calc_batch = i;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("刷新")) {
        vm_.load_calc_batches();
    }

    ImGui::SameLine();
    bool can_calc = vm_.selected_calc_batch >= 0 && !vm_.calc_stream.is_loading();
    if (!can_calc) ImGui::BeginDisabled();
    if (ImGui::Button("计算差值")) {
        vm_.compute_diff();
    }
    if (!can_calc) ImGui::EndDisabled();

    ImGui::Spacing();

    if (vm_.calc_stream.is_loading()) {
        ImGui::Text("正在计算... (%d/%d)", vm_.calc_stream.completed(), vm_.calc_stream.total());
    }

    if (vm_.calc_stream.count() > 0) {
        auto guard = vm_.calc_stream.lock();
        auto& items = vm_.calc_stream.items();

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
                if (!item.loading)
                    ImGui::Text("%ld", item.current);
                else
                    ImGui::TextDisabled("--");
                ImGui::TableSetColumnIndex(2);
                if (!item.loading)
                    ImGui::Text("%ld", item.base);
                else
                    ImGui::TextDisabled("--");
                ImGui::TableSetColumnIndex(3);
                if (!item.loading)
                    ImGui::Text("%+ld", item.diff);
                else
                    ImGui::TextDisabled("--");
                ImGui::TableSetColumnIndex(4);

                if (item.loading) {
                    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "获取中");
                } else {
                    ImVec4 clr(0.9f, 0.9f, 0.9f, 1.0f);
                    if (item.status.find("异常") != std::string::npos)
                        clr = ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
                    else if (item.status == "正常")
                        clr = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
                    else
                        clr = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                    ImGui::TextColored(clr, "%s", item.status.c_str());
                }
            }
            ImGui::EndTable();
        }
    } else if (!vm_.calc_stream.is_loading()) {
        ImGui::TextDisabled("选择基准批次后点击\"计算差值\"");
    }
}
