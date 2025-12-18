#pragma once

#include <memory>
#include <vector>
#include <imgui.h>
#include "base/application.h"
#include "base/glsl_program.h"
#include "engine/scene.h"
#include "engine/renderer.h"
#include "editor/editor_camera.h"
#include "editor/panels/hierarchy_panel.h"
#include "editor/panels/inspector_panel.h"
#include "editor/panels/project_panel.h"
#include "editor/panels/scene_view_panel.h"
#include "engine/scene_object.h"
#include "engine/outline_pass.h"
#include "engine/resource_manager.h"

class SceneRoaming : public Application
{
public:
    SceneRoaming(const Options &options);
    ~SceneRoaming();

    void handleInput() override {};
    void renderFrame() override;

private:
    std::unique_ptr<Scene> _scene;       // 负责数据
    std::unique_ptr<Renderer> _renderer; // 负责画画

    std::unique_ptr<SceneViewPanel> _sceneViewPanel;
    std::unique_ptr<HierarchyPanel> _hierarchyPanel;
    std::unique_ptr<InspectorPanel> _inspectorPanel;
    std::unique_ptr<ProjectPanel> _projectPanel;

    // 编辑器状态变量
    bool _isLayoutInitialized = false; // 用于只在第一次运行时设置窗口位置
    bool _isProjectOpen = false;
    char _projectPathBuf[256] = "";
    float _contentScale = 1.0f;

    // 选中状态
    GameObject *_selectedObject = nullptr;

    // UI 相关
    void initImGui();
    void renderUI();
    void renderProjectSelector();
    void setupDockspace();
    void updateContentScale();
};