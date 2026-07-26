#include <cstring>
#include "imgui.h"
#include "MachineMgrView.hpp"

void MachineMgrPage::open_add_popup() {
    popup_ = {};
    popup_.port = 8193;
    popup_.show_edit = true;
}

void MachineMgrPage::open_edit_popup(const MachineInfo& m) {
    popup_ = {};
    popup_.edit_id = m.id;
    strncpy(popup_.name, m.name.c_str(), sizeof(popup_.name) - 1);
    strncpy(popup_.ip, m.ip.c_str(), sizeof(popup_.ip) - 1);
    popup_.port = m.port;
    popup_.show_edit = true;
}

void MachineMgrPage::draw_edit_popup() {
    if (!popup_.show_edit) return;

    ImGui::OpenPopup(popup_.edit_id ? "编辑机床" : "添加新机床");
    if (ImGui::BeginPopupModal(popup_.edit_id ? "编辑机床" : "添加新机床", &popup_.show_edit,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("名称", popup_.name, sizeof(popup_.name));
        ImGui::InputText("IP 地址", popup_.ip, sizeof(popup_.ip));
        ImGui::InputInt("端口", &popup_.port);
        if (popup_.port <= 0 || popup_.port > 65535) popup_.port = 8193;

        ImGui::Spacing();
        if (ImGui::Button("确定", ImVec2(120, 0))) {
            if (popup_.name[0] && popup_.ip[0]) {
                if (popup_.edit_id)
                    vm_.update(popup_.edit_id, popup_.name, popup_.ip, popup_.port);
                else
                    vm_.add(popup_.name, popup_.ip, popup_.port);
            }
            popup_.show_edit = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) {
            popup_.show_edit = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MachineMgrPage::draw_delete_confirm() {
    if (!popup_.show_delete_confirm) return;

    ImGui::OpenPopup("确认删除");
    if (ImGui::BeginPopupModal("确认删除", &popup_.show_delete_confirm,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("确定要删除该机床吗？");
        ImGui::Spacing();
        if (ImGui::Button("删除", ImVec2(120, 0))) {
            vm_.remove(popup_.delete_target_id);
            popup_.show_delete_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) {
            popup_.show_delete_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MachineMgrPage::draw(AppState& state) {
    vm_.load();

    ImGui::Text("机床管理");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("添加机床")) {
        open_add_popup();
    }
    ImGui::Spacing();

    if (ImGui::BeginTable("##machines", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("IP 地址", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("端口", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableHeadersRow();

        for (auto& m : vm_.machines) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(m.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(m.ip.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", m.port);
            ImGui::TableSetColumnIndex(3);

            char btn_id[32];
            snprintf(btn_id, sizeof(btn_id), "编辑##%d", m.id);
            if (ImGui::SmallButton(btn_id))
                open_edit_popup(m);
            ImGui::SameLine();
            snprintf(btn_id, sizeof(btn_id), "删除##%d", m.id);
            if (ImGui::SmallButton(btn_id)) {
                popup_.delete_target_id = m.id;
                popup_.show_delete_confirm = true;
            }
        }
        ImGui::EndTable();
    }

    if (vm_.machines.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("暂无机床数据，请添加");
    }

    draw_edit_popup();
    draw_delete_confirm();
}
