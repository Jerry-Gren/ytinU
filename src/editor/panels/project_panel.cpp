#include "project_panel.h"
#include <imgui.h>
#include <algorithm>
#include <iostream>

ProjectPanel::ProjectPanel() : Panel("Project / Assets") {}

void ProjectPanel::onImGuiRender()
{
    if (!_isOpen) return;

    if (!ImGui::Begin(_title.c_str(), &_isOpen)) {
        ImGui::End();
        return;
    }

    // =========================================================
    // 1. 顶部工具栏 (Toolbar)
    // =========================================================
    {
        // 刷新按钮
        if (ImGui::Button("Refresh")) {
            ResourceManager::Get().refreshProjectDirectory();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reload file list from disk (F5)");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // 显示当前路径信息
        std::string root = ResourceManager::Get().getProjectRoot();
        if (root.empty()) root = "(No Project Open)";
        ImGui::TextDisabled("%s", root.c_str());

        // 快捷键支持 (当窗口聚焦或鼠标悬停时)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
                ResourceManager::Get().refreshProjectDirectory();
            }
        }

        ImGui::Separator();
    }

    // =========================================================
    // 2. 资源列表 (Grid Layout)
    // =========================================================
    
    // 给内容区域留出一点边距
    ImGui::Dummy(ImVec2(0, 5));

    const auto& files = ResourceManager::Get().getFileList();
    
    float padding = 10.0f;
    float thumbnailSize = 80.0f;
    float cellSize = thumbnailSize + padding;
    float panelWidth = ImGui::GetContentRegionAvail().x;
    
    // 至少显示一列，防止除零或负数
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    // 使用 ID 避免冲突
    if (ImGui::BeginTable("AssetGrid", columnCount))
    {
        for (const auto& file : files)
        {
            std::string filename = file.first;
            std::string relativePath = file.second;

            std::string ext = std::filesystem::path(filename).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            bool isModel = (ext == ".obj" || ext == ".gltf" || ext == ".glb");
            bool isTexture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                              ext == ".bmp" || ext == ".tga" || ext == ".hdr");

            ImGui::TableNextColumn();
            ImGui::PushID(relativePath.c_str());

            // 绘制大图标按钮 (暂时用 Button 模拟，后续可以换成真正的图标纹理)
            // 区分颜色以简单识别类型
            if (isModel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.2f, 0.5f, 1.0f)); // 紫色代表模型
            else if (isTexture) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.2f, 1.0f)); // 绿色代表图片
            else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f)); // 灰色其他

            // 按钮
            ImGui::Button(isModel ? "MODEL" : (isTexture ? "TEX" : "FILE"), ImVec2(thumbnailSize, thumbnailSize));
            
            ImGui::PopStyleColor();

            // 拖拽源 (Drag Source)
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                if (isModel) {
                    ImGui::SetDragDropPayload("ASSET_OBJ", relativePath.c_str(), relativePath.size() + 1);
                    ImGui::Text("Model: %s", filename.c_str());
                }
                else if (isTexture) {
                    ImGui::SetDragDropPayload("ASSET_TEXTURE", relativePath.c_str(), relativePath.size() + 1);
                    ImGui::Text("Texture: %s", filename.c_str());
                }
                ImGui::EndDragDropSource();
            }

            // 文件名截断显示 (防止太长破坏布局)
            std::string label = filename;
            if (label.length() > 12) label = label.substr(0, 9) + "...";
            
            // 居中文件名
            float textWidth = ImGui::CalcTextSize(label.c_str()).x;
            float offset = (thumbnailSize - textWidth) * 0.5f;
            if (offset > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
            
            ImGui::Text("%s", label.c_str());
            if (ImGui::IsItemHovered() && label != filename) {
                ImGui::SetTooltip("%s", filename.c_str());
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
}