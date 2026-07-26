#include <cstring>
#include "imgui.h"
#include "MachineDetailView.hpp"

void MachineDetailPage::draw(AppState& state) {
    int machine_id = state.selected_machine_id;

    if (machine_id <= 0) {
        ImGui::TextDisabled("请选择一台机床");
        return;
    }

    if (vm_.current_name.empty() && !vm_.data.is_loading() && !vm_.data.is_loaded()) {
        vm_.fetch(machine_id);
    }
    if (!vm_.data.is_loaded() && !vm_.data.is_loading() && !vm_.current_name.empty()) {
        vm_.fetch(machine_id);
    }

    if (!vm_.current_name.empty()) {
        ImGui::Text("%s - 机床详情", vm_.current_name.c_str());
    }
    ImGui::Separator();
    ImGui::Spacing();

    if (vm_.data.is_loading()) {
        ImGui::Text("正在获取数据...");
        return;
    }

    if (!vm_.data.is_loaded()) return;

    auto guard = vm_.data.lock();
    auto& d = vm_.data.data();

    if (!d.ok) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
            "连接失败: %s", d.error_msg.c_str());
        if (ImGui::Button("重新连接")) {
            vm_.data.reset();
            vm_.fetch(machine_id);
        }
        return;
    }

    if (ImGui::BeginTable("##sysinfo", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("项目", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("系统类型");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%c%c / %.4s",
            d.sys.cnc_type[0], d.sys.cnc_type[1], d.sys.series);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("版本");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%.4s / 最大轴数: %d",
            d.sys.version, d.sys.max_axis);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("运行状态");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(d.status.run
            ? ImVec4(0.2f, 0.8f, 0.4f, 1.0f)
            : ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
            "%s", d.status.run ? "运行中" : "停止");
        ImGui::SameLine();
        ImGui::Text("  报警: %s  急停: %s",
            d.status.alarm ? "是" : "否",
            d.status.emergency ? "是" : "否");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("进给/主轴");
        ImGui::TableSetColumnIndex(1); ImGui::Text("进给: %ld  主轴: %ld",
            d.act.feedrate, d.act.spindle);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("加工数量");
        ImGui::TableSetColumnIndex(1); ImGui::Text("当前: %ld  要求: %ld  累计: %ld",
            d.part_count.current, d.part_count.required, d.part_count.total);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("程序号");
        ImGui::TableSetColumnIndex(1); ImGui::Text("程序号: %d / 主程序: %d",
            d.prog.prg_number, d.prog.prg_main);

        if (d.prog.prg_name[0]) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("当前程序");
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(d.prog.prg_name);
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("序列/块信息");
        ImGui::TableSetColumnIndex(1); ImGui::Text("序列号: %ld  块计数: %ld",
            d.prog.seq_number, d.prog.blk_count);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("动态轴数");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%d", d.dyn.axis);

        ImGui::EndTable();
    }

    if (d.alarms.count > 0) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "报警信息:");
        for (int i = 0; i < d.alarms.count; i++) {
            ImGui::BulletText("#%ld [轴%c] %s",
                d.alarms.alarm_no[i], d.alarms.axis[i], d.alarms.msg[i]);
        }
    }

    if (d.pos.count > 0) {
        ImGui::Spacing();
        ImGui::Text("轴位置:");
        if (ImGui::BeginTable("##positions", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("轴");
            ImGui::TableSetupColumn("绝对");
            ImGui::TableSetupColumn("机械");
            ImGui::TableSetupColumn("相对");
            ImGui::TableHeadersRow();

            const char* names = "XYZABCUVW";
            for (int i = 0; i < d.pos.count && i < CNC_MAX_AXES; i++) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%c轴", names[i]);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.4f", d.pos.absolute[i]);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.4f", d.pos.machine[i]);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.4f", d.pos.relative[i]);
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("刷新数据")) {
        vm_.data.reset();
        vm_.fetch(machine_id);
    }
}
