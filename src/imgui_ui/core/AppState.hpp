#pragma once
#include <string>

enum class UiPage {
    Overview,
    MachineMgr,
    MachineDetail,
    HistoryBrowse,
    HistoryCalc
};

struct AppState {
    UiPage current_page = UiPage::Overview;
    int selected_machine_id = 0;
    std::string selected_machine_name;

    void navigate(UiPage p) { current_page = p; }

    void select_machine(int id, std::string name) {
        selected_machine_id = id;
        selected_machine_name = std::move(name);
        current_page = UiPage::MachineDetail;
    }
};
