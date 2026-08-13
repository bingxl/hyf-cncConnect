#pragma once
#include "core/IPage.hpp"
#include "viewmodel/MachiningVm.hpp"

class MachiningStatsPage : public IPage {
public:
    UiPage id() const override { return UiPage::MachiningStats; }
    const char* label() const override { return "加工统计"; }
    void draw(AppState& state) override;
private:
    MachiningVm vm_;
    bool machines_loaded_ = false;

    void draw_controls();
    void draw_bucket_table();
    void draw_product_table();
};
