#pragma once

#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "base/camera.h"
#include "engine/physics_utils.h"

// 前置声明
class GameObject; 
class Scene;
struct ImDrawList;

class EditorCamera
{
public:
    EditorCamera(int width, int height);
    ~EditorCamera() = default;

    // --- 核心更新 ---
    // 每帧调用，处理输入和平滑插值
    void update(float deltaTime);
    
    // 当窗口大小改变时调用
    void onResize(int width, int height);

    // --- 输入处理 ---
    // 接管 ImGui 的输入
    void handleInput(const glm::vec3& scenePivot = glm::vec3(0.0f));

    // --- 功能接口 ---
    Camera* getActiveCamera() const { return _cameras[_activeCameraIndex].get(); }
    
    // 获取当前的 Pivot (注视点)
    glm::vec3 getPivot() const { return _pivot; }

    // 聚焦物体 (对应原来的 frameSelectedObject)
    void frameObject(GameObject* obj);

    // 切换视角 (对应原来的 switchToView)
    void switchToView(const glm::vec3& dir);

    // 屏幕射线 (对应原来的 screenPointToRay)
    // 需要传入视口的位置和大小 (ImGui Image 的 Rect)
    Ray screenPointToRay(float mouseX, float mouseY, float viewportX, float viewportY, float viewportW, float viewportH);

    // 绘制右上角的 View Gizmo (返回是否被 Hover)
    bool drawViewGizmo(const glm::vec2& viewportPos, const glm::vec2& viewportSize);

    // 用于外部查询是否正在操作相机 (SceneRoaming 可以用它来决定是否绘制选择框等)
    bool isControlling() const { return _isControlling; }

private:
    // --- 内部状态 (从 SceneRoaming 搬过来的) ---
    std::vector<std::unique_ptr<Camera>> _cameras;
    int _activeCameraIndex = 0;

    glm::vec3 _pivot = glm::vec3(0.0f);
    glm::vec2 _smoothOrbitDelta = glm::vec2(0.0f);
    
    // 平滑缩放变量
    float _currentOrbitDist = 15.0f;
    float _targetOrbitDist = 15.0f;

    // 动画变量
    bool _isAnimating = false;
    float _animTime = 0.0f;
    float _animDuration = 0.3f;
    glm::vec3 _animStartPos, _animTargetPos;
    glm::vec3 _animStartPivot, _animTargetPivot;
    glm::quat _animStartRot, _animTargetRot;

    // Gizmo 拖拽状态
    bool _isGizmoDragging = false;
    bool _isControlling = false;

    // --- 内部辅助函数 ---
    void rotateCamera(float dx, float dy);
    void startAnimation(const glm::vec3& targetPos, const glm::quat& targetRot, const glm::vec3& targetPivot);
    void updateAnimation(float dt);
    void updateSmoothZoom(float dt);
    
    // 那个很长的绘制 Gizmo 的函数
    glm::vec3 drawGizmoInternal(ImDrawList* drawList, const glm::vec2& center, float size, bool& outHovered);
};