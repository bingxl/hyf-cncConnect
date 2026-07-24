#include <cstdio>
#include <cstring>
#include "imgui.h"
#include "ui_machine_mgr.h"
#include "db_ops.h"

extern DbHandle g_db;

static char edit_name[DB_MAX_NAME] = "";
static char edit_ip[DB_MAX_IP] = "";
static int  edit_port = 8193;
static int  edit_id = 0;
static bool show_edit_popup = false;
static bool show_delete_confirm = false;
static int  delete_target_id = 0;

static void open_add_popup(void)
{
    edit_id = 0;
    edit_name[0] = '\0';
    edit_ip[0] = '\0';
    edit_port = 8193;
    show_edit_popup = true;
}

static void open_edit_popup(int id)
{
    MachineRecord rec;
    if (db_get_machine_by_id(g_db, id, &rec) == 0) {
        edit_id = rec.id;
        strncpy(edit_name, rec.name, DB_MAX_NAME - 1);
        strncpy(edit_ip, rec.ip, DB_MAX_IP - 1);
        edit_port = rec.port;
        show_edit_popup = true;
    }
}

static void draw_edit_popup(void)
{
    if (!show_edit_popup) return;

    ImGui::OpenPopup(edit_id ? "编辑机床" : "添加新机床");
    if (ImGui::BeginPopupModal(edit_id ? "编辑机床" : "添加新机床", &show_edit_popup,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("名称", edit_name, DB_MAX_NAME);
        ImGui::InputText("IP 地址", edit_ip, DB_MAX_IP);
        ImGui::InputInt("端口", &edit_port);
        if (edit_port <= 0 || edit_port > 65535) edit_port = 8193;

        ImGui::Spacing();
        if (ImGui::Button("确定", ImVec2(120, 0))) {
            if (edit_name[0] && edit_ip[0]) {
                if (edit_id)
                    db_update_machine(g_db, edit_id, edit_name, edit_ip, edit_port);
                else
                    db_add_machine(g_db, edit_name, edit_ip, edit_port);
            }
            show_edit_popup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) {
            show_edit_popup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void draw_delete_confirm(void)
{
    if (!show_delete_confirm) return;

    ImGui::OpenPopup("确认删除");
    if (ImGui::BeginPopupModal("确认删除", &show_delete_confirm,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("确定要删除该机床吗？");
        ImGui::Spacing();
        if (ImGui::Button("删除", ImVec2(120, 0))) {
            db_delete_machine(g_db, delete_target_id);
            show_delete_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) {
            show_delete_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ui_machine_mgr_draw(void)
{
    MachineRecord machines[DB_MAX_MACHINES];
    int count = 0;

    if (g_db)
        count = db_get_machines(g_db, machines, DB_MAX_MACHINES);

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

        for (int i = 0; i < count; i++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(machines[i].name);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(machines[i].ip);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", machines[i].port);
            ImGui::TableSetColumnIndex(3);

            char btn_id[32];
            snprintf(btn_id, sizeof(btn_id), "编辑##%d", machines[i].id);
            if (ImGui::SmallButton(btn_id))
                open_edit_popup(machines[i].id);
            ImGui::SameLine();
            snprintf(btn_id, sizeof(btn_id), "删除##%d", machines[i].id);
            if (ImGui::SmallButton(btn_id)) {
                delete_target_id = machines[i].id;
                show_delete_confirm = true;
            }
        }
        ImGui::EndTable();
    }

    if (count == 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("暂无机床数据，请添加");
    }

    draw_edit_popup();
    draw_delete_confirm();
}
