#include "hierarchy_panel.h"
#include <imgui.h>
#include <imgui_internal.h>

HierarchyPanel::HierarchyPanel() : Panel("Scene Hierarchy") {}

void HierarchyPanel::onImGuiRender(const std::unique_ptr<Scene>& scene, GameObject*& selectedObject)
{
    if (!_isOpen) return;

    // [还原] 不再强制去除 WindowPadding，直接使用 EditorStyle 中定义的全局 Padding
    // 这样 Hierarchy 面板的边缘间距将与其他面板（如 Inspector）完全一致
    if (!ImGui::Begin(_title.c_str(), &_isOpen)) {
        ImGui::End();
        return;
    }

    // =========================================================
    // 1. 顶部工具栏
    // =========================================================
    {
        // [还原] 不再需要手动 SetCursorPos，ImGui 会自动根据 WindowPadding 放置起始位置
        
        if (ImGui::Button("+ Add Object")) {
            ImGui::OpenPopup("AddObjPopup_Toolbar");
        }

        ImGui::SameLine();
        
        // 自动计算剩余宽度
        float availWidth = ImGui::GetContentRegionAvail().x; 
        ImGui::SetNextItemWidth(availWidth);
        
        static char searchBuf[64] = "";
        ImGui::InputTextWithHint("##Search", "Search...", searchBuf, sizeof(searchBuf));

        // 分割线
        ImGui::Separator();
    }

    if (ImGui::BeginPopup("AddObjPopup_Toolbar"))
    {
        if (ImGui::MenuItem("Cube")) scene->createCube(); 
        if (ImGui::MenuItem("Point Light")) scene->createPointLight();
        ImGui::EndPopup();
    }

    // =========================================================
    // 2. Hierarchy 列表 (Table)
    // =========================================================
    
    // PadOuterX: 依然保留，让斑马纹背景左右稍微延伸一点点，视觉上更柔和
    static ImGuiTableFlags table_flags = 
        ImGuiTableFlags_Resizable | 
        ImGuiTableFlags_RowBg | 
        ImGuiTableFlags_ScrollY | 
        ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_PadOuterX;

    // [保留垂直修正] 仅保留垂直方向的 CellPadding，用于解决你之前提到的“文字重叠”问题
    // 水平方向(X)设为 4.0f (标准值)，因为现在外部已经有 WindowPadding 了，不需要额外的缩进
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 4.0f));

    // 计算表格高度：-1 表示占据剩余所有空间
    // 之前可能因为 WindowPadding=0 导致布局计算复杂，现在可以直接传 ImVec2(0, 0)
    if (ImGui::BeginTable("HierarchyTable", 2, table_flags))
    {
        // 表头
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        
        // --- 遍历物体 ---
        const auto& objects = scene->getGameObjects();
        int idToDelete = -1;

        for (int i = 0; i < objects.size(); ++i)
        {
            auto &go = objects[i];
            
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            ImGui::PushID(go->getInstanceID());

            // SpanAllColumns: 依然保留，让选中高亮条横跨整个表格
            ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;

            bool isSelected = (selectedObject == go.get());
            
            // 垂直居中对齐
            ImGui::AlignTextToFramePadding();

            // 绘制行
            if (ImGui::Selectable(go->name.c_str(), isSelected, selectable_flags))
            {
                selectedObject = go.get();
            }

            // 右键菜单
            if (ImGui::BeginPopupContextItem())
            {
                selectedObject = go.get();
                if (ImGui::MenuItem("Delete")) {
                    idToDelete = i;
                }
                ImGui::Separator();
                ImGui::MenuItem("Duplicate (TODO)", nullptr, false, false);
                ImGui::EndPopup();
            }

            // 第二列：类型
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding(); 

            const char* typeStr = "-";
            if (go->getComponent<LightComponent>()) typeStr = "Light";
            else if (go->getComponent<MeshComponent>()) typeStr = "Mesh";
            else if (go->getComponent<ReflectionProbeComponent>()) typeStr = "Probe";

            ImGui::TextDisabled("%s", typeStr);

            ImGui::PopID();
        }

        if (idToDelete != -1) {
            scene->markForDestruction(objects[idToDelete].get());
            if (selectedObject == objects[idToDelete].get()) selectedObject = nullptr;
        }

        ImGui::EndTable();
    }
    
    ImGui::PopStyleVar(); // Pop CellPadding

    // =========================================================
    // 3. 空白处交互
    // =========================================================
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
        selectedObject = nullptr;
    }

    if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        ImGui::TextDisabled("Create Asset");
        ImGui::Separator();
        if (ImGui::MenuItem("Cube")) scene->createCube(); 
        ImGui::Separator();
        if (ImGui::MenuItem("Point Light")) scene->createPointLight();
        ImGui::EndPopup();
    }

    ImGui::End();
}