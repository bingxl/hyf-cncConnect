#pragma once
#include "core/CncData.hpp"
#include "viewmodel/OverviewVm.hpp"
#include "viewmodel/MachineMgrVm.hpp"
#include "viewmodel/MachineDetailVm.hpp"
#include "viewmodel/HistoryVm.hpp"

class MainView {
public:
    void draw(UiPage& page, int& selected_machine_id);

private:
    void draw_sidebar(UiPage& page, int& selected_machine_id);

    OverviewVm overview_vm_;
    MachineMgrVm machine_mgr_vm_;
    MachineDetailVm machine_detail_vm_;
    HistoryVm history_vm_;
};
