#pragma once
#include "panel.h"
#include "engine/scene.h"

class HierarchyPanel : public Panel {
public:
    HierarchyPanel();
    
    // 我们需要传入 Scene 指针来遍历物体
    // 我们需要传入 selectedObject 的引用，以便面板能修改当前选中的物体
    void onImGuiRender(const std::unique_ptr<Scene>& scene, GameObject*& selectedObject);

    // 覆盖基类接口 (虽然主要用上面的带参版本)
    void onImGuiRender() override {} 
};