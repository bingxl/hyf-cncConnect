#pragma once
#include "core/IPage.hpp"
#include "viewmodel/OverviewVm.hpp"

class OverviewPage : public IPage {
public:
    UiPage id() const override { return UiPage::Overview; }
    const char* label() const override { return "机床总览"; }
    void draw(AppState& state) override;

private:
    OverviewVm vm_;
};
