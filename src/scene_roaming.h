#pragma once

#include <memory>
#include <vector>

#include "base/application.h"
#include "base/camera.h"
#include "base/glsl_program.h"
#include "engine/scene.h"
#include "engine/renderer.h"
#include "scene_object.h"
#include "outline_pass.h"
#include "physics_utils.h"
#include "resource_manager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

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

    // 编辑器状态变量
    bool _isLayoutInitialized = false; // 用于只在第一次运行时设置窗口位置
    bool _isProjectOpen = false;
    char _projectPathBuf[256] = "";

    // 摄像机 (属于编辑器视角的相机，不属于场景)
    std::vector<std::unique_ptr<Camera>> _cameras;
    int activeCameraIndex = 0;

    // 选中状态
    GameObject *_selectedObject = nullptr;

    // UI 相关
    void initImGui();
    void renderUI();
    void renderProjectSelector();
    void renderProjectPanel();
    void drawInspector(GameObject *obj);
    void drawComponentUI(Component *comp);

    // 交互与 Gizmo
    float _contentScale = 1.0f;
    void updateContentScale();
    bool _firstMouse = true;
    void handleMousePick();
    Ray screenPointToRay(float mouseX, float mouseY);

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
    
    // 相机动画相关
    bool _isCameraAnimating = false;
    float _animTime = 0.0f;
    float _animDuration = 0.3f; // 动画时长 0.3秒
    
    glm::vec3 _animStartPos;
    glm::vec3 _animTargetPos;
    glm::quat _animStartRot;
    glm::quat _animTargetRot;
    glm::vec3 _animStartPivot;  // 动画开始时的注视点
    glm::vec3 _animTargetPivot; // 动画结束时的注视点
    glm::vec3 _cameraPivot; // 摄像机焦点

    // 平滑缩放相关变量
    float _currentOrbitDist = 15.0f; // 当前实际距离
    float _targetOrbitDist = 15.0f;  // 目标距离 (用于插值)
    
    // 聚焦选中物体
    void frameSelectedObject();
    
    // 重新计算 Pivot (用于从 WASD 模式切回 Orbit 模式时)
    void recalculatePivot();

    // 每帧计算平滑缩放
    void updateSmoothZoom();
    
    // 切换到指定视图 (Front/Right/Top)
    void switchToView(const glm::vec3& dir);

    // 辅助函数：开始一段相机动画
    void startCameraAnimation(const glm::vec3& targetPos, const glm::quat& targetRot, const glm::vec3& targetPivot);

    // 辅助函数：每帧更新相机位置
    void updateCameraAnimation();

    // 修改 drawViewGizmo 的签名，让它返回被点击的轴的方向（如果没有点击则返回 0向量）
    // 返回值：glm::vec3 (点击的轴方向，例如 {1,0,0})，如果没点中返回 {0,0,0}
    bool _isGizmoDragging = false;
    glm::vec3 drawViewGizmo(ImDrawList* drawList, const glm::vec3& cameraPos, const glm::mat4& viewMatrix, ImVec2 centerPos, float axisLength, bool& outGizmoHovered);

    // 通用旋转函数 (dx, dy 为屏幕空间的位移量)
    void rotateCamera(float dx, float dy);
};