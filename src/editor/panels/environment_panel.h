#pragma once
#include "panel.h"
#include "engine/scene.h"

class EnvironmentPanel : public Panel {
public:
    EnvironmentPanel() : Panel("Environment") {}

    void onImGuiRender(Scene* scene, Renderer* renderer) {
        if (!_isOpen || !scene) return;

        if (ImGui::Begin(_title.c_str(), &_isOpen)) {
            auto& env = scene->getEnvironment();

            // 1. 模式选择
            const char* typeNames[] = { "Procedural Sky", "HDR Map" };
            int currentType = (int)env.type;
            if (ImGui::Combo("Type", &currentType, typeNames, 2)) {
                env.type = (SkyboxType)currentType;
            }

            ImGui::Separator();

            if (env.type == SkyboxType::Procedural) {
                ImGui::Text("Procedural Colors");
                ImGui::ColorEdit3("Zenith", &env.skyZenithColor.x);
                ImGui::ColorEdit3("Horizon", &env.skyHorizonColor.x);
                ImGui::ColorEdit3("Ground", &env.groundColor.x);
                ImGui::DragFloat("Energy", &env.skyEnergy, 0.1f, 0.0f, 10.0f);
            } 
            else if (env.type == SkyboxType::CubeMap) {
                // 显示当前路径
                std::string filename = std::filesystem::path(env.hdrFilePath).filename().string();
                if (filename.empty()) filename = "(Drag .hdr file here)";
                
                ImGui::Button(filename.c_str(), ImVec2(-1, 40));

                if (ImGui::BeginDragDropTarget()) {
                    // 接受任意文件，这里假设用户拖的是 .hdr
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE")) {
                        const char* path = (const char*)payload->Data;
                        std::string strPath = path;
                        // 简单检查扩展名
                        if (strPath.find(".hdr") != std::string::npos) {
                            env.hdrFilePath = strPath;
                            // 触发加载
                            renderer->loadSkyboxHDR(env.hdrFilePath);
                        } else {
                            std::cout << "[UI] Only .hdr files are supported for Skybox!" << std::endl;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::DragFloat("Energy", &env.skyEnergy, 0.1f, 0.0f, 10.0f);
            }

            ImGui::Separator();
            ImGui::DragFloat("Global Exposure", &env.globalExposure, 0.1f, 0.1f, 10.0f);
        }
        ImGui::End();
    }
    
    // 占位实现
    void onImGuiRender() override {}
};