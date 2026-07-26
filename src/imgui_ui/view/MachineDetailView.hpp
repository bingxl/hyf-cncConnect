#pragma once
#include "core/IPage.hpp"
#include "viewmodel/MachineDetailVm.hpp"

class MachineDetailPage : public IPage {
public:
    UiPage id() const override { return UiPage::MachineDetail; }
    const char* label() const override { return "查看详情"; }
    void draw(AppState& state) override;

private:
    MachineDetailVm vm_;
};
