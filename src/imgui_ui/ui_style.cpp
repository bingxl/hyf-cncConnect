#include <cstdio>
#include "imgui.h"
#include "ui_style.h"

void ui_style_init(void)
{
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();

    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.ScrollbarRounding = 8.0f;
    style.TabRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.WindowPadding = ImVec2(14, 14);
    style.FramePadding = ImVec2(10, 5);
    style.ItemSpacing = ImVec2(10, 8);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 12.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    ImVec4 *c = style.Colors;

    /*  -- 基础背景 --  */
    c[ImGuiCol_WindowBg]              = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.12f, 0.13f, 0.15f, 0.96f);

    /*  -- 边框 --  */
    c[ImGuiCol_Border]                = ImVec4(0.22f, 0.24f, 0.28f, 0.60f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    /*  -- 文字 --  */
    c[ImGuiCol_Text]                  = ImVec4(0.88f, 0.90f, 0.93f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.52f, 0.54f, 1.00f);

    /*  -- 标题栏 --  */
    c[ImGuiCol_TitleBg]               = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.10f, 0.11f, 0.13f, 0.75f);

    /*  -- 菜单栏 --  */
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);

    /*  -- 滚动条 --  */
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.10f, 0.11f, 0.13f, 0.55f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.22f, 0.24f, 0.28f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.28f, 0.36f, 0.44f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.32f, 0.44f, 0.54f, 1.00f);

    /*  -- 复选框/选中标记 --  */
    c[ImGuiCol_CheckMark]             = ImVec4(0.20f, 0.60f, 0.90f, 1.00f);

    /*  -- 滑块 --  */
    c[ImGuiCol_SliderGrab]            = ImVec4(0.20f, 0.60f, 0.90f, 0.80f);
    c[ImGuiCol_SliderGrabActive]      = ImVec4(0.28f, 0.68f, 0.98f, 1.00f);

    /*  -- 按钮 --  */
    c[ImGuiCol_Button]                = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.24f, 0.36f, 0.50f, 1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.20f, 0.52f, 0.78f, 1.00f);

    /*  -- 标题头 --  */
    c[ImGuiCol_Header]                = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.24f, 0.36f, 0.50f, 0.80f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.20f, 0.52f, 0.78f, 1.00f);

    /*  -- 分隔线 --  */
    c[ImGuiCol_Separator]             = ImVec4(0.22f, 0.24f, 0.28f, 0.50f);
    c[ImGuiCol_SeparatorHovered]      = ImVec4(0.20f, 0.52f, 0.78f, 0.60f);
    c[ImGuiCol_SeparatorActive]       = ImVec4(0.20f, 0.60f, 0.90f, 1.00f);

    /*  -- 标签页 --  */
    c[ImGuiCol_Tab]                   = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered]            = ImVec4(0.24f, 0.36f, 0.50f, 0.80f);
    c[ImGuiCol_TabSelected]           = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]   = ImVec4(0.20f, 0.60f, 0.90f, 1.00f);
    c[ImGuiCol_TabDimmed]             = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]     = ImVec4(0.16f, 0.20f, 0.26f, 1.00f);

    /*  -- 表格 --  */
    c[ImGuiCol_TableHeaderBg]         = ImVec4(0.16f, 0.20f, 0.26f, 1.00f);
    c[ImGuiCol_TableBorderStrong]     = ImVec4(0.22f, 0.24f, 0.28f, 1.00f);
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(0.14f, 0.15f, 0.17f, 0.50f);

    /*  -- Resize Grip --  */
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.20f, 0.60f, 0.90f, 0.15f);
    c[ImGuiCol_ResizeGripHovered]     = ImVec4(0.20f, 0.60f, 0.90f, 0.50f);
    c[ImGuiCol_ResizeGripActive]      = ImVec4(0.20f, 0.60f, 0.90f, 0.80f);

}

void ui_load_fonts(void)
{
    ImGuiIO &io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;

    const char *font_path = "C:\\Windows\\Fonts\\msyh.ttc";
    FILE *f = NULL;
    fopen_s(&f, font_path, "rb");
    if (f) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF(font_path, 16.0f, &cfg,
            io.Fonts->GetGlyphRangesChineseFull());
    } else {
        io.Fonts->AddFontDefault(&cfg);
    }
}
