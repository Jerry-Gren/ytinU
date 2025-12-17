#pragma once
#include <string>
#include <imgui.h>

class Panel {
public:
    Panel(const std::string& title) : _title(title) {}
    virtual ~Panel() = default;

    // 核心绘制函数
    // 子类实现具体的 ImGui::Begin() ... End() 逻辑
    virtual void onImGuiRender() = 0;

    // 显示/隐藏控制
    void open() { _isOpen = true; }
    void close() { _isOpen = false; }
    bool isOpen() const { return _isOpen; }
    
    // 设置是否可见的引用 (用于 MenuItem 的 bool*)
    bool* getOpenPtr() { return &_isOpen; }

protected:
    std::string _title;
    bool _isOpen = true;
};