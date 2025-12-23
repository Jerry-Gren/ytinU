#include "editor_style.h"
#include "engine/resource_manager.h"
#include <filesystem>
#include <iostream>
#include <imgui_internal.h>

void EditorStyle::init(float contentScale) {
    // 1. 加载字体
    loadFonts(contentScale);

    // 2. 应用主题
    applyTheme();

    // 3. 全局缩放 (根据 DPI)
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(contentScale);
}

void EditorStyle::loadFonts(float contentScale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // 基础字号 (根据 DPI 缩放)
    float fontSize = 16.0f * contentScale;
    
    // 尝试加载项目内的字体
    std::string fontPath = ResourceManager::Get().getFullPath("media/fonts/Roboto-Regular.ttf");
    
    ImFontConfig config;
    config.OversampleH = 3; // 水平过采样，让字体更清晰
    config.OversampleV = 3; // 垂直过采样

    if (std::filesystem::exists(fontPath)) {
        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize, &config);
        std::cout << "[UI] Loaded custom font: " << fontPath << std::endl;
    } else {
        // 回退默认
        io.Fonts->AddFontDefault();
        std::cout << "[UI] Custom font not found, using default." << std::endl;
    }
}

void EditorStyle::applyTheme() {
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    // =========================================================
    // 1. 布局与尺寸 (Layout & Sizes)
    // =========================================================
    // 这种设置让 UI 看起来更像生产力工具，而不是 Debug 菜单
    
    // 圆角：现代 UI 倾向于小圆角，主窗口通常无圆角以最大化拼接
    style->WindowRounding    = 4.0f; 
    style->ChildRounding     = 4.0f;
    style->FrameRounding     = 4.0f; // 输入框和按钮的圆角
    style->GrabRounding      = 3.0f; // 滑块抓手的圆角
    style->PopupRounding     = 4.0f;
    style->ScrollbarRounding = 12.0f; // 胶囊型滚动条
    style->TabRounding       = 4.0f;

    // 边框：扁平化设计通常只需要极细的边框
    style->WindowBorderSize  = 1.0f; 
    style->ChildBorderSize   = 1.0f;
    style->PopupBorderSize   = 1.0f;
    style->FrameBorderSize   = 0.0f; // 输入框默认无边框，靠背景色区分
    style->TabBorderSize     = 0.0f;

    // 间距：让 UI 呼吸感更强
    style->WindowPadding     = ImVec2(10.0f, 10.0f);
    style->FramePadding      = ImVec2(5.0f, 5.0f);      // 让按钮和输入框稍微高一点
    style->ItemSpacing       = ImVec2(8.0f, 6.0f);      // 控件之间的垂直间距稍大
    style->ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style->IndentSpacing     = 20.0f;                   // 树状列表缩进
    style->ScrollbarSize     = 14.0f;                   // 稍微宽一点的滚动条，易于点击
    style->GrabMinSize       = 12.0f;

    // 标题栏对齐
    style->WindowTitleAlign  = ImVec2(0.5f, 0.5f);      // 居中标题
    style->ButtonTextAlign   = ImVec2(0.5f, 0.5f);

    // =========================================================
    // 2. 配色方案 (Color Palette) - "Deep Dark Professional"
    // =========================================================
    
    // 基础色调定义 (方便微调)
    const ImVec4 col_text_main      = ImVec4(0.90f, 0.90f, 0.93f, 1.00f); // 略带冷色的白
    const ImVec4 col_text_disabled  = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    
    const ImVec4 col_win_bg         = ImVec4(0.12f, 0.12f, 0.12f, 1.00f); // 主窗口背景 #1F1F1F
    const ImVec4 col_child_bg       = ImVec4(0.15f, 0.15f, 0.15f, 1.00f); // 子面板背景
    const ImVec4 col_popup_bg       = ImVec4(0.12f, 0.12f, 0.12f, 0.98f); // 弹出层背景
    
    const ImVec4 col_border         = ImVec4(0.25f, 0.25f, 0.25f, 0.50f); // 极淡的边框
    const ImVec4 col_border_shadow  = ImVec4(0.00f, 0.00f, 0.00f, 0.00f); // 现代 UI 很少用硬阴影

    // 控件背景 (输入框、滑块槽) - 比背景色更深，形成凹陷感
    const ImVec4 col_frame_bg       = ImVec4(0.08f, 0.08f, 0.08f, 1.00f); 
    const ImVec4 col_frame_hover    = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    const ImVec4 col_frame_active   = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);

    // 标题栏 (Title Bar) - 沉浸式
    const ImVec4 col_title_bg       = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    const ImVec4 col_title_active   = ImVec4(0.10f, 0.10f, 0.10f, 1.00f); // 激活时不改变颜色，避免闪烁
    const ImVec4 col_title_collapse = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);

    // 核心强调色 (Accent Color) - 类似 UE5 的科技蓝
    // 如果你喜欢 Blender 风格，可以把这里改为橙色系 (e.g. 0.9f, 0.45f, 0.1f)
    const ImVec4 col_accent         = ImVec4(0.16f, 0.48f, 0.82f, 1.00f); // #297BD1
    const ImVec4 col_accent_hover   = ImVec4(0.22f, 0.58f, 0.95f, 1.00f);
    const ImVec4 col_accent_active  = ImVec4(0.12f, 0.40f, 0.70f, 1.00f);

    // 按钮 (Button) - 默认深灰色，不抢眼
    const ImVec4 col_btn            = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    const ImVec4 col_btn_hover      = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    const ImVec4 col_btn_active     = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);

    // 标签页 (Tabs) - 关键在于非激活状态要“退后”，激活状态要“对齐”
    const ImVec4 col_tab            = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    const ImVec4 col_tab_hover      = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    const ImVec4 col_tab_active     = col_child_bg; // 激活的 Tab 应该和 ChildBg 融为一体
    const ImVec4 col_tab_unfocused  = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    const ImVec4 col_tab_unfocused_active = col_child_bg;

    // --- 应用颜色 ---
    
    // 文本
    colors[ImGuiCol_Text]                   = col_text_main;
    colors[ImGuiCol_TextDisabled]           = col_text_disabled;
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.40f);

    // 窗口与背景
    colors[ImGuiCol_WindowBg]               = col_win_bg;
    colors[ImGuiCol_ChildBg]                = col_child_bg;
    colors[ImGuiCol_PopupBg]                = col_popup_bg;
    colors[ImGuiCol_Border]                 = col_border;
    colors[ImGuiCol_BorderShadow]           = col_border_shadow;

    // 输入框与Frame
    colors[ImGuiCol_FrameBg]                = col_frame_bg;
    colors[ImGuiCol_FrameBgHovered]         = col_frame_hover;
    colors[ImGuiCol_FrameBgActive]          = col_frame_active;

    // 标题栏
    colors[ImGuiCol_TitleBg]                = col_title_bg;
    colors[ImGuiCol_TitleBgActive]          = col_title_active;
    colors[ImGuiCol_TitleBgCollapsed]       = col_title_collapse;
    colors[ImGuiCol_MenuBarBg]              = col_title_bg;

    // 滚动条 (更加极简)
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.00f); // 透明背景
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);

    // 核心交互元素 (Checkmark, Slider, Drag) - 使用强调色
    colors[ImGuiCol_CheckMark]              = col_accent;
    colors[ImGuiCol_SliderGrab]             = col_accent;
    colors[ImGuiCol_SliderGrabActive]       = col_accent_active;

    // 按钮
    colors[ImGuiCol_Button]                 = col_btn;
    colors[ImGuiCol_ButtonHovered]          = col_btn_hover;
    colors[ImGuiCol_ButtonActive]           = col_btn_active;

    // Header (TreeNode, Selectable) - 类似资源管理器
    // 这里的颜色不宜太亮，否则 Hierarchy 会显得很乱
    colors[ImGuiCol_Header]                 = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.20f); // 淡淡的选中色
    colors[ImGuiCol_HeaderHovered]          = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.35f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.50f);

    // 分隔线 (Separator)
    colors[ImGuiCol_Separator]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = col_accent;
    colors[ImGuiCol_SeparatorActive]        = col_accent_active;
    
    // 调整手柄 (Resize Grip)
    colors[ImGuiCol_ResizeGrip]             = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.60f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.90f);

    // 标签页 (Tabs)
    colors[ImGuiCol_Tab]                    = col_tab;
    colors[ImGuiCol_TabHovered]             = col_tab_hover;
    colors[ImGuiCol_TabActive]              = col_tab_active;
    colors[ImGuiCol_TabUnfocused]           = col_tab_unfocused;
    colors[ImGuiCol_TabUnfocusedActive]     = col_tab_unfocused_active;

    // 停靠 (Docking)
    colors[ImGuiCol_DockingPreview]         = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.70f); // 拖拽时的预览色
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

    // 图表 (Plot / Graph)
    colors[ImGuiCol_PlotLines]              = col_text_main;
    colors[ImGuiCol_PlotLinesHovered]       = col_accent;
    colors[ImGuiCol_PlotHistogram]          = col_accent;
    colors[ImGuiCol_PlotHistogramHovered]   = col_accent_hover;

    // 表格 (Table) - 现代 UI 必备
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.25f, 0.25f, 0.25f, 0.20f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.02f); // 极淡的斑马纹

    // 拖放导航
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 0.80f, 0.00f, 0.90f); // 鲜艳的黄色提示
    colors[ImGuiCol_NavHighlight]           = col_accent; // 键盘导航高亮
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.60f); // 模态窗口背后的遮罩更黑
}