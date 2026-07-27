#pragma once
#include "core/IPage.hpp"
#include "viewmodel/HistoryVm.hpp"

class HistorySavePage : public IPage {
public:
    UiPage id() const override { return UiPage::HistorySave; }
    const char* label() const override { return "保存批次"; }
    void draw(AppState& state) override;
private:
    HistoryVm vm_;
};

class HistoryBrowsePage : public IPage {
public:
    UiPage id() const override { return UiPage::HistoryBrowse; }
    const char* label() const override { return "批次浏览"; }
    void draw(AppState& state) override;
private:
    HistoryVm vm_;

    struct EditState {
        bool show = false;
        int entry_id = 0;
        char name[64] = "";
        int required = 0;
        int current = 0;
        int total = 0;
    } edit_;

    struct DeleteConfirmState {
        bool show = false;
        int entry_id = 0;
        int batch_id = 0;
        bool is_batch = false;
    } del_;

    void draw_batch_list();
    void draw_batch_detail();
    void draw_edit_popup();
    void draw_delete_confirm();
};

class HistoryCalcPage : public IPage {
public:
    UiPage id() const override { return UiPage::HistoryCalc; }
    const char* label() const override { return "差值计算"; }
    void draw(AppState& state) override;
private:
    HistoryVm vm_;
};
