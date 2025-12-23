#pragma once
#include "panel.h"
#include "engine/scene.h"
#include "engine/renderer.h"

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
                SkyboxType oldType = env.type;
                env.type = (SkyboxType)currentType;
            }

            ImGui::Separator();

            if (env.type == SkyboxType::Procedural) {
                ImGui::Text("Procedural Colors");

                // 定义一个辅助 lambda，用于绘制控件并检测状态
                // 返回 true 表示用户刚刚完成了编辑（松开鼠标）
                auto DrawColorControl = [&](const char* label, float* col3) -> bool {
                    ImGui::ColorEdit3(label, col3);
                    // 只有当用户松开鼠标或回车确认时，才返回 true
                    return ImGui::IsItemDeactivatedAfterEdit();
                };

                bool editFinished = false;

                // 绘制控件，env 的值会实时改变，所以背景会实时变色（因为 drawSkybox 每帧都读 env）
                // 但是 editFinished 只有在松手时才会变 true
                editFinished |= DrawColorControl("Zenith", &env.skyZenithColor.x);
                editFinished |= DrawColorControl("Horizon", &env.skyHorizonColor.x);
                editFinished |= DrawColorControl("Ground", &env.groundColor.x);
                
                // Energy 也是同理
                ImGui::DragFloat("Energy", &env.skyEnergy, 0.1f, 0.0f, 10.0f);
                if (ImGui::IsItemDeactivatedAfterEdit()) editFinished = true;

                // 只有在编辑动作“完成”时，才触发昂贵的 IBL 烘焙
                if (editFinished) {
                    renderer->updateProceduralSkybox(env);
                }
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