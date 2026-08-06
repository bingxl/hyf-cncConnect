#include <cstring>
#include <cstdio>
#include "imgui.h"
#include "MachineDetailView.hpp"
#include "core/Database.hpp"

static void kv_row(const char *key, const char *fmt, ...)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(key);
    ImGui::TableSetColumnIndex(1);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

static void kv_color(const char *key, const ImVec4 &color, const char *fmt, ...)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(key);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
    ImGui::PopStyleColor();
}

static const char* mode_name(int aut, int tmmode, int edit)
{
    if (aut == 1) {
        switch (tmmode) {
            case 0: return "自动 (内存)";
            case 1: return "自动 (DNC)";
            case 2: return "自动 (MDI)";
            default: return "自动";
        }
    }
    if (edit) return "编辑";
    if (tmmode == 2) return "MDI";
    return "手动";
}

static const char* motion_name(int motion)
{
    switch (motion) {
        case 0: return "就绪";
        case 1: return "运动中";
        case 2: return "暂停";
        case 3: return "等待辅助";
        default: return "未知";
    }
}

void MachineDetailPage::draw(AppState& state)
{
    int machine_id = state.selected_machine_id;

    /* ---- Machine selector + refresh toolbar ---- */
    {
        auto machines = Database::instance().get_machines();

        std::string current_label;
        const char *preview = "请选择机床";
        for (auto& m : machines) {
            if (m.id == machine_id) {
                current_label = m.name + "  (" + m.ip + ":" + std::to_string(m.port) + ")";
                preview = current_label.c_str();
                break;
            }
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
        if (ImGui::BeginCombo("##machine_sel", preview)) {
            for (auto& m : machines) {
                bool selected = (m.id == machine_id);
                std::string label = m.name + "  (" + m.ip + ":" + std::to_string(m.port) + ")";
                if (ImGui::Selectable(label.c_str(), selected))
                    state.select_machine(m.id, m.name);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(machine_id <= 0);
        if (ImGui::Button("刷新", ImVec2(80, 0))) {
            vm_.data.reset();
            vm_.fetch(machine_id);
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Spacing();
    }

    if (machine_id <= 0) {
        ImGui::TextDisabled("请选择一台机床");
        return;
    }

    auto machine = Database::instance().get_machine(machine_id);

    if (machine_id != vm_.current_machine_id && !vm_.data.is_loading()) {
        vm_.current_machine_id = machine_id;
        vm_.data.reset();
        vm_.fetch(machine_id);
    }

    if (vm_.data.is_loading()) {
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 40);
        ImGui::ProgressBar(-1.0f, ImVec2(w * 0.5f, 0), "正在获取数据...");
        ImGui::SetCursorPosX((w - 200) * 0.5f);
        ImGui::TextDisabled("正在从机床读取数据，请稍候...");
        return;
    }

    if (!vm_.data.is_loaded()) return;

    bool has_error = false;

    /* ---- Render data under lock ---- */
    {
        auto guard = vm_.data.lock();
        auto& d = vm_.data.data();
        has_error = !d.ok;

        if (has_error) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 40);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.3f, 0.3f, 1));
            float w = ImGui::GetContentRegionAvail().x;
            const char *err = d.error_msg.c_str();
            ImGui::SetCursorPosX((w - ImGui::CalcTextSize(err).x) * 0.5f);
            ImGui::TextUnformatted(err);
            ImGui::PopStyleColor();
            ImGui::Spacing();
        } else {
            /* ---- Header ---- */
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                ImGui::Text("%s", vm_.current_name.c_str());
                ImGui::PopStyleColor();
                if (machine) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("  |  %s:%d", machine->ip.c_str(), machine->port);
                }

                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
                if (d.status.run) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.6f, 0.3f, 1));
                    ImGui::Button("● 运行中", ImVec2(80, 0));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1));
                    ImGui::Button("● 已停止", ImVec2(80, 0));
                }
                ImGui::PopStyleColor();

                ImGui::Separator();
                ImGui::Spacing();
            }

            float avail = ImGui::GetContentRegionAvail().x;
            float col_w = (avail - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

            /* ============================
             * Left Column
             * ============================ */
            ImGui::BeginChild("##col_left", ImVec2(col_w, 0), false);

            /* System Info */
            if (ImGui::CollapsingHeader("系统信息", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable("##sys", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("项目", ImGuiTableColumnFlags_WidthFixed, 100);
                    ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthStretch);

                    char cnc_label[8];
                    snprintf(cnc_label, sizeof(cnc_label), "%c%c", d.sys.cnc_type[0], d.sys.cnc_type[1]);
                    kv_row("CNC 类型", "%s / %s", cnc_label, d.sys.mt_type);
                    kv_row("系列", "%.4s", d.sys.series);
                    kv_row("版本", "%.4s", d.sys.version);
                    kv_row("最大轴数", "%d", d.sys.max_axis);
                    if (d.sys.axes[0])
                        kv_row("控制轴", "%s", d.sys.axes);
                    if (d.path_count > 0)
                        kv_row("路径", "当前 %d / 共 %d", d.path_current, d.path_count);

                    ImGui::EndTable();
                }
                ImGui::Spacing();
            }

            /* Status */
            if (ImGui::CollapsingHeader("运行状态", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable("##status", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("项目", ImGuiTableColumnFlags_WidthFixed, 100);
                    ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthStretch);

                    kv_color("运行状态",
                        d.status.run ? ImVec4(0.2f, 0.9f, 0.4f, 1) : ImVec4(1, 0.4f, 0.3f, 1),
                        "%s", d.status.run ? "运行中" : "已停止");
                    kv_row("运行模式", "%s", mode_name(d.status.aut, d.status.tmmode, d.status.edit));
                    kv_row("运动状态", "%s", motion_name(d.status.motion));

                    kv_color("紧急停止",
                        d.status.emergency ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.3f, 0.8f, 0.3f, 1),
                        "%s", d.status.emergency ? "急停触发" : "正常");
                    kv_color("报警状态",
                        d.status.alarm ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.3f, 0.8f, 0.3f, 1),
                        "%s", d.status.alarm ? "有报警" : "无报警");

                    if (d.status.mstb)
                        kv_row("M/S/T/B", "%s", "执行中");
                    ImGui::EndTable();
                }
                ImGui::Spacing();
            }

            /* Feed & Spindle */
            if (ImGui::CollapsingHeader("进给 / 主轴", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable("##act", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("项目", ImGuiTableColumnFlags_WidthFixed, 100);
                    ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthStretch);

                    kv_row("实际进给", "%ld", d.act.feedrate);
                    kv_row("主轴转速", "%ld", d.act.spindle);
                    kv_row("动态进给", "%ld", d.dyn.actf);
                    kv_row("动态主轴", "%ld", d.dyn.acts);
                    ImGui::EndTable();
                }
                ImGui::Spacing();
            }

            /* Parameters */
            if (d.param_6750 || d.setting_0) {
                if (ImGui::CollapsingHeader("参数", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::BeginTable("##param", 2,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("项目", ImGuiTableColumnFlags_WidthFixed, 100);
                        ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthStretch);
                        if (d.param_6750)
                            kv_row("参数 #6750", "%ld (快速进给)", d.param_6750);
                        if (d.setting_0)
                            kv_row("设定 #0", "%ld", d.setting_0);
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                }
            }

            /* Tool Offsets */
            if (d.tool_offsets.count > 0) {
                if (ImGui::CollapsingHeader("刀具偏置", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::BeginTable("##tool", 2,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("编号", ImGuiTableColumnFlags_WidthFixed, 60);
                        ImGui::TableSetupColumn("偏置值", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();
                        for (int i = 0; i < d.tool_offsets.count; i++) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("#%d", i);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.4f", d.tool_offsets.values[i]);
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                }
            }

            ImGui::EndChild();
            ImGui::SameLine();

            /* ============================
             * Right Column
             * ============================ */
            ImGui::BeginChild("##col_right", ImVec2(0, 0), false);

            /* Part Count */
            if (ImGui::CollapsingHeader("加工计数", ImGuiTreeNodeFlags_DefaultOpen)) {
                long cur = d.part_count.current;
                long req = d.part_count.required;
                long total = d.part_count.total;

                if (req > 0) {
                    float pct = (float)cur / req;
                    if (pct > 1) pct = 1;
                    char pct_str[32];
                    snprintf(pct_str, sizeof(pct_str), "%ld / %ld  (%.0f%%)", cur, req, pct * 100);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.6f, 0.9f, 1));
                    ImGui::ProgressBar(pct, ImVec2(-1, 22), pct_str);
                    ImGui::PopStyleColor();
                }

                if (ImGui::BeginTable("##part", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("项目", ImGuiTableColumnFlags_WidthFixed, 100);
                    ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthStretch);
                    kv_row("当前数量", "%ld", cur);
                    kv_row("要求数量", "%ld", req);
                    kv_row("累计数量", "%ld", total);
                    if (cur >= 0 && req > 0) {
                        long remain = req - cur;
                        if (remain < 0) remain = 0;
                        kv_row("剩余数量", "%ld", remain);
                    }
                    ImGui::EndTable();
                }
                ImGui::Spacing();
            }

            /* Program Info */
            if (ImGui::CollapsingHeader("程序信息", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable("##prog", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("项目", ImGuiTableColumnFlags_WidthFixed, 100);
                    ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthStretch);

                    kv_row("程序号", "%d", d.prog.prg_number);
                    kv_row("主程序号", "%d", d.prog.prg_main);
                    if (d.prog.prg_name[0])
                        kv_row("程序名称", "%s", d.prog.prg_name);
                    kv_row("序列号", "%ld", d.prog.seq_number);
                    kv_row("块计数", "%ld", d.prog.blk_count);
                    if (d.dyn.seqnum)
                        kv_row("动态序列", "%ld", d.dyn.seqnum);
                    if (d.dyn.prgnum)
                        kv_row("动态程序号", "%ld", d.dyn.prgnum);
                    if (d.dyn.prgmnum)
                        kv_row("动态主程序", "%ld", d.dyn.prgmnum);

                    ImGui::EndTable();
                }
                ImGui::Spacing();
            }

            /* Program List */
            if (d.prog_list.count > 0) {
                if (ImGui::CollapsingHeader("程序列表", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::BeginTable("##progs", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                            ImVec2(0, 120))) {
                        ImGui::TableSetupColumn("程序号", ImGuiTableColumnFlags_WidthFixed, 70);
                        ImGui::TableSetupColumn("长度", ImGuiTableColumnFlags_WidthFixed, 60);
                        ImGui::TableSetupColumn("注释", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();
                        for (int i = 0; i < d.prog_list.count; i++) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%ld", d.prog_list.number[i]);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%ld", d.prog_list.length[i]);
                            ImGui::TableSetColumnIndex(2);
                            if (d.prog_list.comment[i][0])
                                ImGui::TextUnformatted(d.prog_list.comment[i]);
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                }
            }

            /* Alarms */
            if (d.alarms.count > 0) {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.6f, 0.15f, 0.15f, 1));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.7f, 0.2f, 0.2f, 1));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                bool open = ImGui::CollapsingHeader("报警信息", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::PopStyleColor(3);
                if (open) {
                    for (int i = 0; i < d.alarms.count && i < CNC_MAX_ALARMS; i++) {
                        char label[64];
                        snprintf(label, sizeof(label), "%ld", d.alarms.alarm_no[i]);
                        ImGui::BulletText("报警 %s  [轴 %c]  %s",
                            label, d.alarms.axis[i], d.alarms.msg[i]);
                    }
                    ImGui::Spacing();
                }
            }

            /* Macro Variables */
            if (d.macro_vars.count > 0) {
                if (ImGui::CollapsingHeader("宏变量 (#1 - #10)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::BeginTable("##macro", 2,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("变量", ImGuiTableColumnFlags_WidthFixed, 60);
                        ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();
                        for (int i = 0; i < d.macro_vars.count; i++) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("#%d", i + 1);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.4f", d.macro_vars.values[i]);
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                }
            }

            /* Work Zero Offsets */
            if (d.work_zero.count > 0) {
                if (ImGui::CollapsingHeader("工件零点偏置", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const char *names = "XYZABCUVW";
                    if (ImGui::BeginTable("##wzero", 2,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("轴", ImGuiTableColumnFlags_WidthFixed, 40);
                        ImGui::TableSetupColumn("偏置值", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();
                        for (int i = 0; i < d.work_zero.count && i < CNC_MAX_AXES; i++) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%c", names[i]);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.4f", d.work_zero.values[i]);
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                }
            }

            ImGui::EndChild();

            /* ============================
             * Full Width: Axis Positions
             * ============================ */
            ImGui::Spacing();
            const char *names = "XYZABCUVW";
            if (d.pos.count > 0 && ImGui::CollapsingHeader("轴位置", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable("##axes", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("轴", ImGuiTableColumnFlags_WidthFixed, 40);
                    ImGui::TableSetupColumn("绝对位置", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("机械位置", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("相对位置", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("剩余距离", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < d.pos.count && i < CNC_MAX_AXES; i++) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%c", names[i]);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.4f", d.pos.absolute[i]);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.4f", d.pos.machine[i]);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%.4f", d.pos.relative[i]);
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%.4f", d.pos.distance[i]);
                    }
                    ImGui::EndTable();
                }
                ImGui::Spacing();
            }
        }
    } /* lock released */

    /* ---- Buttons (outside lock) ---- */
    float w = ImGui::GetContentRegionAvail().x;
    if (has_error) {
        ImGui::SetCursorPosX((w - 80) * 0.5f);
        if (ImGui::Button("重新连接", ImVec2(80, 0))) {
            vm_.data.reset();
            vm_.fetch(machine_id);
        }
        return;
    }
}
