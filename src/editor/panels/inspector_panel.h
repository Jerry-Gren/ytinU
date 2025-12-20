#pragma once
#include "panel.h"
#include "engine/scene_object.h"
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

    // 通用资源槽绘制函数
    // label: 属性名 (如 "Diffuse Map")
    // currentName: 当前资源的显示名称 (如 "box.png" 或 "(None)")
    // fullPath: 完整路径 (用于 Tooltip 显示)
    // payloadType: 拖拽类型 (如 "ASSET_OBJ")
    // onDrop: 接收到拖拽时的回调
    // onClear: 点击清除时的回调
    void drawResourceSlot(const char* label, 
                          const std::string& currentName, 
                          const std::string& fullPath, 
                          const char* payloadType,
                          std::function<void(const std::string&)> onDrop,
                          std::function<void()> onClear);
};