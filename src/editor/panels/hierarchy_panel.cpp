#include "hierarchy_panel.h"
#include <imgui.h>

HierarchyPanel::HierarchyPanel() : Panel("Scene Hierarchy") {}

void HierarchyPanel::onImGuiRender(const std::unique_ptr<Scene>& scene, GameObject*& selectedObject)
{
    if (!_isOpen) return;

    // 注意：ImGui::Begin 需要传入指针来控制关闭按钮
    if (!ImGui::Begin(_title.c_str(), &_isOpen)) {
        ImGui::End();
        return;
    }

    // 1. 添加物体按钮
    if (ImGui::Button("+ Add Object"))
        ImGui::OpenPopup("AddObjPopup");
    
    if (ImGui::BeginPopup("AddObjPopup"))
    {
        if (ImGui::MenuItem("Cube")) {
            scene->createCube(); 
        }
        if (ImGui::MenuItem("Point Light")) {
            scene->createPointLight();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // 2. 遍历物体列表
    const auto& objects = scene->getGameObjects();
    
    for (int i = 0; i < objects.size(); ++i)
    {
        auto &go = objects[i];
        
        // 使用 Selectable 模拟列表项
        // 未来如果要支持拖拽层级，这里需要改用 ImGui::TreeNode
        std::string label = go->name + "##" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), selectedObject == go.get()))
        {
            selectedObject = go.get();
        }
    }

    // 3. 点击空白处取消选择
    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
        selectedObject = nullptr;

    ImGui::End();
}