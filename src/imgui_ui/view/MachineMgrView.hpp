#pragma once
#include "core/IPage.hpp"
#include "viewmodel/MachineMgrVm.hpp"

class MachineMgrPage : public IPage {
public:
    UiPage id() const override { return UiPage::MachineMgr; }
    const char* label() const override { return "机床管理"; }
    void draw(AppState& state) override;

private:
    MachineMgrVm vm_;

    struct PopupState {
        char name[64] = "";
        char ip[64] = "";
        int port = 8193;
        int edit_id = 0;
        bool show_edit = false;
        bool show_delete_confirm = false;
        int delete_target_id = 0;
    };

    PopupState popup_;

    void open_add_popup();
    void open_edit_popup(const MachineInfo& m);
    void draw_edit_popup();
    void draw_delete_confirm();
};
