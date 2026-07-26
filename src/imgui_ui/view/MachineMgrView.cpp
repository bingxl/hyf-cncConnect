#include <cstring>
#include "imgui.h"
#include "MachineMgrView.hpp"

struct PopupState {
    char name[64] = "";
    char ip[64] = "";
    int port = 8193;
    int edit_id = 0;
    bool show_edit = false;
    bool show_delete_confirm = false;
    int delete_target_id = 0;
};

static PopupState s_popup;

static void open_add_popup() {
    s_popup = {};
    s_popup.port = 8193;
    s_popup.show_edit = true;
}

static void open_edit_popup(const MachineInfo& m) {
    s_popup = {};
    s_popup.edit_id = m.id;
    strncpy(s_popup.name, m.name.c_str(), sizeof(s_popup.name) - 1);
    strncpy(s_popup.ip, m.ip.c_str(), sizeof(s_popup.ip) - 1);
    s_popup.port = m.port;
    s_popup.show_edit = true;
}

static void draw_edit_popup(MachineMgrVm& vm) {
    if (!s_popup.show_edit) return;

    ImGui::OpenPopup(s_popup.edit_id ? "编辑机床" : "添加新机床");
    if (ImGui::BeginPopupModal(s_popup.edit_id ? "编辑机床" : "添加新机床", &s_popup.show_edit,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("名称", s_popup.name, sizeof(s_popup.name));
        ImGui::InputText("IP 地址", s_popup.ip, sizeof(s_popup.ip));
        ImGui::InputInt("端口", &s_popup.port);
        if (s_popup.port <= 0 || s_popup.port > 65535) s_popup.port = 8193;

        ImGui::Spacing();
        if (ImGui::Button("确定", ImVec2(120, 0))) {
            if (s_popup.name[0] && s_popup.ip[0]) {
                if (s_popup.edit_id)
                    vm.update(s_popup.edit_id, s_popup.name, s_popup.ip, s_popup.port);
                else
                    vm.add(s_popup.name, s_popup.ip, s_popup.port);
            }
            s_popup.show_edit = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) {
            s_popup.show_edit = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void draw_delete_confirm(MachineMgrVm& vm) {
    if (!s_popup.show_delete_confirm) return;

    ImGui::OpenPopup("确认删除");
    if (ImGui::BeginPopupModal("确认删除", &s_popup.show_delete_confirm,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("确定要删除该机床吗？");
        ImGui::Spacing();
        if (ImGui::Button("删除", ImVec2(120, 0))) {
            vm.remove(s_popup.delete_target_id);
            s_popup.show_delete_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) {
            s_popup.show_delete_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void machine_mgr_view_draw(MachineMgrVm& vm) {
    vm.load();

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

        for (auto& m : vm.machines) {
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
                s_popup.delete_target_id = m.id;
                s_popup.show_delete_confirm = true;
            }
        }
        ImGui::EndTable();
    }

    if (vm.machines.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("暂无机床数据，请添加");
    }

    draw_edit_popup(vm);
    draw_delete_confirm(vm);
}
