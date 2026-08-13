#include <cstring>
#include <ctime>
#include "imgui.h"
#include "MachiningView.hpp"

namespace {
void format_duration(long secs, char* out, int size) {
    int h = (int)(secs / 3600);
    int m = (int)((secs % 3600) / 60);
    int s = (int)(secs % 60);
    snprintf(out, size, "%02d:%02d:%02d", h, m, s);
}
}

void MachiningStatsPage::draw(AppState& state) {
    (void)state;
    ImGui::Text("加工统计");
    ImGui::Separator();
    ImGui::TextWrapped("按机床和程序 comment(产品编号) 统计实际加工时间与加工件数。数据来自常驻采集程序写入的数据库。");
    ImGui::Spacing();

    if (!machines_loaded_) {
        vm_.load_machines();
        machines_loaded_ = true;
    }

    draw_controls();
    ImGui::Separator();
    ImGui::Spacing();

    if (!vm_.loaded && !vm_.message.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", vm_.message.c_str());
        return;
    }

    if (vm_.loaded) {
        char total_buf[32];
        format_duration(vm_.total_machining_secs, total_buf, sizeof(total_buf));
        ImGui::Text("总加工时间: %s", total_buf);
        ImGui::SameLine();
        ImGui::TextDisabled("  |  总加工件数: %ld  |  时间段: %d 个半小时区间",
                            vm_.total_produced, (int)vm_.buckets.size());
        ImGui::Spacing();
        draw_product_table();
        ImGui::Spacing();
        draw_bucket_table();
    } else {
        ImGui::TextDisabled("选择机床和时间范围后点击\"查询\"");
    }
}

void MachiningStatsPage::draw_controls() {
    if (vm_.machines.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f),
                           "尚未配置机床，请在\"机床管理\"中添加。");
        return;
    }

    // machine combo
    ImGui::Text("机床:");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##machine", vm_.machines[vm_.selected_machine_index].name.c_str())) {
        for (int i = 0; i < (int)vm_.machines.size(); i++) {
            bool selected = (i == vm_.selected_machine_index);
            if (ImGui::Selectable(vm_.machines[i].name.c_str(), selected)) {
                vm_.selected_machine_index = i;
                vm_.machine_id = vm_.machines[i].id;
                vm_.machine_name = vm_.machines[i].name;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("今日")) vm_.apply_quick_range("today");
    ImGui::SameLine();
    if (ImGui::Button("昨日")) vm_.apply_quick_range("yesterday");
    ImGui::SameLine();
    if (ImGui::Button("白班 8:30-20:30")) vm_.apply_quick_range("shift_day");
    ImGui::SameLine();
    if (ImGui::Button("夜班 20:30-8:30")) vm_.apply_quick_range("shift_night");

    ImGui::Spacing();
    ImGui::Text("开始:"); ImGui::SameLine(); ImGui::Text("%s", vm_.range_valid ? "已选择" : "未选择");
    ImGui::SameLine();
    ImGui::Text("结束:"); ImGui::SameLine(); ImGui::Text("%s", vm_.range_valid ? "已选择" : "未选择");
    ImGui::SameLine();
    if (ImGui::Button("查询")) vm_.refresh();

    ImGui::SameLine();
    if (ImGui::Button("导出 CSV") && vm_.loaded) {
        std::string path = vm_.default_export_path();
        if (vm_.export_csv(path))
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "已导出: %s", path.c_str());
        else
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "导出失败");
    }
    ImGui::SameLine();
    if (vm_.loaded)
        ImGui::TextDisabled("%s", vm_.default_export_path());
}

void MachiningStatsPage::draw_product_table() {
    ImGui::Text("按产品(程序 comment) 统计");
    if (vm_.products.empty()) {
        ImGui::TextDisabled("该时间段无加工数据");
        return;
    }
    if (ImGui::BeginTable("##prod", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("产品编号", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("加工时长", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("加工件数", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("占比", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("秒", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        for (auto& r : vm_.products) {
            char buf[32];
            format_duration(r.machining_secs, buf, sizeof(buf));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(r.comment.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", buf);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%ld", r.produced);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.1f%%", r.share * 100.0);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%ld", r.machining_secs);
        }
        ImGui::EndTable();
    }
}

void MachiningStatsPage::draw_bucket_table() {
    ImGui::Text("半小时区间明细");
    if (vm_.buckets.empty()) {
        ImGui::TextDisabled("无明细数据");
        return;
    }
    if (ImGui::BeginTable("##bucket", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("时间区间", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("加工样本数", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("加工时长", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("加工件数", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("秒", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        for (auto& r : vm_.buckets) {
            char tbuf[32];
            time_t tt = (time_t)r.ts_bucket;
            tm lt{};
            localtime_s(&lt, &tt);
            strftime(tbuf, sizeof(tbuf), "%m-%d %H:%M", &lt);
            char dbuf[32];
            format_duration(r.machining_secs, dbuf, sizeof(dbuf));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", tbuf);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", r.sample_count);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", dbuf);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%ld", r.produced);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%ld", r.machining_secs);
        }
        ImGui::EndTable();
    }
}
