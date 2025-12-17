#include "project_panel.h"
#include <imgui.h>

ProjectPanel::ProjectPanel() : Panel("Project / Assets") {}

void ProjectPanel::onImGuiRender()
{
    if (!_isOpen) return;

    if (!ImGui::Begin(_title.c_str(), &_isOpen)) {
        ImGui::End();
        return;
    }

    const auto& files = ResourceManager::Get().getFileList();
    
    float padding = 10.0f;
    float thumbnailSize = 80.0f;
    float cellSize = thumbnailSize + padding;
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::Columns(columnCount, 0, false);

    for (const auto& file : files)
    {
        std::string filename = file.first;
        std::string relativePath = file.second;

        ImGui::PushID(relativePath.c_str());

        // 按钮代表文件
        ImGui::Button(filename.c_str(), ImVec2(80, 80));

        // 拖拽源
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("ASSET_OBJ", relativePath.c_str(), relativePath.size() + 1);
            ImGui::Text("Model: %s", filename.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::TextWrapped("%s", filename.c_str());
        ImGui::PopID();
        ImGui::NextColumn();
    }

    ImGui::Columns(1);
    ImGui::End();
}