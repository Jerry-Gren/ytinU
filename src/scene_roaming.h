#pragma once

#include <memory>
#include <vector>
#include <imgui.h>
#include "base/application.h"
#include "base/glsl_program.h"
#include "engine/scene.h"
#include "engine/renderer.h"
#include "editor/editor_camera.h"
#include "scene_object.h"
#include "outline_pass.h"
#include "resource_manager.h"

class SceneRoaming : public Application
{
public:
    SceneRoaming(const Options &options);
    ~SceneRoaming();

    void handleInput() override;
    void renderFrame() override;

private:
    std::unique_ptr<Scene> _scene;       // 负责数据
    std::unique_ptr<Renderer> _renderer; // 负责画画
    std::unique_ptr<EditorCamera> _cameraController; // 相机控制器

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
    void renderProjectPanel();
    void drawInspector(GameObject *obj);
    void drawComponentUI(Component *comp);
    void updateContentScale();

    void handleMousePick();

    // Scene FBO (这是编辑器为了显示画面而持有的“画布”)
    struct SceneFBO {
        GLuint fbo = 0;
        GLuint texture = 0;
        GLuint rbo = 0; // 深度缓冲
        int width = 0;
        int height = 0;
    } _sceneFbo;

    void initSceneFBO(int width, int height);
    void resizeSceneFBO(int width, int height);

    ImVec2 _viewportPos = ImVec2(0, 0);  // 3D 视口图片左上角的屏幕坐标
    ImVec2 _viewportSize = ImVec2(0, 0); // 3D 视口图片的大小
    bool _isViewportFocused = false; // 3D视口是否获得焦点(用于键盘输入)
    bool _isViewportHovered = false; // 鼠标是否悬停在 3D 视口内

    // 渲染场景入口
    void renderScene();
};