#pragma once
#include "panel.h"
#include "scene_object.h"
#include "engine/scene.h"

class InspectorPanel : public Panel {
public:
    InspectorPanel();
    
    // 渲染选中物体的属性
    void onImGuiRender(GameObject*& selectedObject, Scene* sceneContext);

    void onImGuiRender() override {}

private:
    // 内部辅助函数：绘制组件列表
    void drawComponents(GameObject* obj);
    
    // 内部辅助函数：绘制单个组件的具体 UI
    void drawComponentUI(Component* comp);
};