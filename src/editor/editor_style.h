#pragma once
#include <imgui.h>
#include <string>

class EditorStyle {
public:
    // 初始化字体和主题
    static void init(float contentScale);

    // 应用自定义配色方案 (类似 UE5/Blender)
    static void applyTheme();

    // 辅助：加载字体 (支持合并图标字体，如 FontAwesome，如果有的话)
    static void loadFonts(float contentScale);

    // 辅助：自动计算宽度的按钮，防止文字溢出
    // 如果 text 宽度超过 availWidth，会自动截断并显示 "..."
    static bool ButtonCenteredOnLine(const char* label, float alignment = 0.5f);
};