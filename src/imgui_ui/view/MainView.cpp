#include "imgui.h"
#include "MainView.hpp"
#include "core/PageRegistry.hpp"

void MainView::draw_sidebar(AppState& state) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "CNC 监控系统");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    for (auto& page : PageRegistry::instance().all()) {
        if (page->id() == UiPage::MachineDetail) continue;
        bool active = (state.current_page == page->id());
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 0.9f));
        }
        if (ImGui::Button(page->label(), ImVec2(-1, 32)))
            state.navigate(page->id());
        if (active)
            ImGui::PopStyleColor(2);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (state.selected_machine_id > 0) {
        ImGui::TextDisabled("当前选中");
        ImGui::TextWrapped("%s", state.selected_machine_name.c_str());
        if (ImGui::SmallButton("查看详情"))
            state.navigate(UiPage::MachineDetail);
    }
}

void MainView::draw(AppState& state) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##MainWindow", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    float sidebar_w = 170.0f;
    ImGui::BeginChild("##Sidebar", ImVec2(sidebar_w, 0), true);
    draw_sidebar(state);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##Content", ImVec2(0, 0), true);
    {
        IPage* page = PageRegistry::instance().get(state.current_page);
        if (page)
            page->draw(state);
    }
    ImGui::EndChild();

    ImGui::End();
}
