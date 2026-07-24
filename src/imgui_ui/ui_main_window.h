#ifndef UI_MAIN_WINDOW_H
#define UI_MAIN_WINDOW_H

enum class UiPage {
    Overview,
    MachineMgr,
    MachineDetail,
    HistoryView,
    HistorySave,
    HistoryCalc,
};

void ui_main_window_draw(UiPage &current_page, int &selected_machine_id);

#endif
