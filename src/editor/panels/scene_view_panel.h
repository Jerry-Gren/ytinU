#pragma once
#include "panel.h"
#include <memory>
#include <glad/gl.h>
#include "editor/editor_camera.h"
#include "engine/renderer.h"
#include "engine/scene.h"

class SceneViewPanel : public Panel {
public:
    SceneViewPanel();
    ~SceneViewPanel();

    // 核心绘制函数
    // 需要传入 Scene 和 Renderer，因为面板只负责"显示"，不负责"拥有"数据
    void onImGuiRender(Scene* scene, Renderer* renderer, GameObject*& selectedObject, float contentScale);

    // 2. [关键修复] 必须覆盖基类的纯虚函数，否则此类为抽象类
    // 给一个空实现即可，因为我们不会通过 Panel* 多态指针来调用这个函数
    void onImGuiRender() override {}

    // 处理输入 (键盘/鼠标)
    // 之前在 SceneRoaming::handleInput 里的逻辑移到这里
    void onInputUpdate(float dt, Scene* scene, GameObject*& selectedObject);

    // 获取内部的相机 (供外部查询，如 Renderer 需要相机矩阵)
    Camera* getCamera() const { return _cameraController->getActiveCamera(); }

private:
    std::unique_ptr<EditorCamera> _cameraController;

    // FBO 相关资源
    struct FrameBuffer {
        GLuint id = 0;
        GLuint texture = 0;
        GLuint rbo = 0;
        int width = 0;
        int height = 0;
    } _fbo;

    // 视口状态
    ImVec2 _viewportPos = {0, 0};
    ImVec2 _viewportSize = {0, 0};
    bool _isHovered = false;
    bool _isFocused = false;
    bool _isControlling = false;

    // 内部辅助
    void initFBO(int width, int height);
    void resizeFBO(int width, int height);
    void handleMousePick(Scene* scene, GameObject*& selectedObject);
};