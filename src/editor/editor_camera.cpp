#include "editor_camera.h"
#include "base/camera.h"
#include "scene_object.h" // 为了访问 GameObject 的 Transform
#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <glm/gtx/vector_angle.hpp>
#include <algorithm> // for std::sort

// 那个 GizmoAxisData 结构体也可以搬到这里来
struct GizmoAxisData {
    glm::vec3 dir;       // 原始方向
    ImU32 mainColor;     // 主颜色 (外圈或实心)
    ImU32 fillColor;     // 填充颜色 (仅负轴使用，稍淡)
    char label;          // 标签文字 ('X', 'Y', 'Z' 或 0)
    bool isNegative;     // 是否是负轴
    float zDepth;        // 变换后的深度 (用于排序)
    ImVec2 screenPos;    // 变换后的屏幕位置
};

#include "editor_camera.h"
#include <glm/gtc/matrix_transform.hpp> // for glm::lookAt
#include <glm/gtc/quaternion.hpp>     // for glm::quat_cast

EditorCamera::EditorCamera(int width, int height)
{
    // 1. 计算宽高比 (使用传入的参数)
    float aspect = (float)width / (float)height;
    
    // 2. 初始化相机容器
    _cameras.resize(2);

    constexpr float znear = 0.1f;
    constexpr float zfar = 10000.0f;

    // =============================================
    // Setup Perspective Camera (Index 0)
    // =============================================
    _cameras[0] = std::make_unique<PerspectiveCamera>(glm::radians(60.0f), aspect, znear, zfar);

    // [搬运] 初始状态：看着原点，距离 15 米
    _pivot = glm::vec3(0.0f, 0.5f, 0.0f); // 原 _cameraPivot
    
    // [搬运] 稍微抬高一点角度
    glm::vec3 startPos = glm::vec3(0.0f, 5.0f, 15.0f);
    
    // [搬运] 初始化平滑缩放变量
    _currentOrbitDist = glm::length(startPos - _pivot);
    _targetOrbitDist = _currentOrbitDist;
    
    // [搬运] 设置相机位置
    _cameras[0]->transform.position = startPos;

    // [搬运] 计算初始旋转 (LookAt)
    // 注意：这里需要 include <glm/gtc/matrix_transform.hpp>
    glm::mat4 view = glm::lookAt(_cameras[0]->transform.position, _pivot, glm::vec3(0, 1, 0));
    _cameras[0]->transform.rotation = glm::quat_cast(glm::inverse(view));

    // =============================================
    // Setup Orthographic Camera (Index 1)
    // =============================================
    _cameras[1] = std::make_unique<OrthographicCamera>(-4.0f * aspect, 4.0f * aspect, -4.0f, 4.0f, znear, zfar);
    _cameras[1]->transform.position = glm::vec3(0.0f, 0.0f, 15.0f);
    
    // 默认激活透视相机
    _activeCameraIndex = 0;
}

void EditorCamera::update(float deltaTime)
{
    updateSmoothZoom(deltaTime);
    updateAnimation(deltaTime);
}

// [搬运来源] SceneRoaming::renderUI 中处理窗口大小变化的逻辑
void EditorCamera::onResize(int width, int height)
{
    float aspect = (float)width / (float)height;
    if (auto pCam = dynamic_cast<PerspectiveCamera *>(_cameras[_activeCameraIndex].get())) {
        pCam->aspect = aspect;
    }
    // 如果有正交相机也需要更新
    if (auto oCam = dynamic_cast<OrthographicCamera *>(_cameras[1].get())) {
        oCam->left = -4.0f * aspect;
        oCam->right = 4.0f * aspect;
    }
}

void EditorCamera::handleInput(const glm::vec3& scenePivot)
{
    // 1. [互斥锁] 如果正在拖拽 Gizmo，绝对不要处理相机旋转/平移
    if (_isGizmoDragging) return;

    ImGuiIO& io = ImGui::GetIO();
    
    // 2. 如果 ImGui 想要捕获键盘（例如在输入框打字），不处理快捷键
    if (io.WantCaptureKeyboard) return;

    Camera* cam = getActiveCamera();
    float dt = io.DeltaTime;
    const float friction = 30.0f;

    // =========================================================
    // 输入获取
    // =========================================================
    float dx = io.MouseDelta.x;
    float dy = io.MouseDelta.y;
    float scrollX = io.MouseWheelH;
    float scrollY = io.MouseWheel;

    bool isShift = io.KeyShift;
    bool isCtrl = io.KeyCtrl;
    bool isLMB = io.MouseDown[0];
    bool isRMB = io.MouseDown[1];
    bool isMMB = io.MouseDown[2];

    // =========================================================
    // [Blender 风格] 设备推断逻辑
    // =========================================================
    bool isFractional = (scrollY != 0.0f) && (std::abs(scrollY - std::round(scrollY)) > 0.02f);
    bool hasHorizontal = (scrollX != 0.0f);
    bool isMouseStep = (std::abs(scrollY) >= 0.9f);
    
    // 判定是否为物理鼠标滚轮
    bool isPhysicalMouse = isMouseStep && !hasHorizontal && !isFractional;

    // =========================================================
    // 意图定义
    // =========================================================
    bool intentZoom = false;
    bool intentOrbit = false;
    bool intentPan = false;

    // 缩放：Ctrl + 滚轮/触摸板，或者 物理滚轮直接滚动
    if (isCtrl || (isPhysicalMouse && !isShift)) {
        intentZoom = true;
    }
    // 平移：Shift + 中键/触摸板
    else if (isShift) {
        intentPan = true;
    }
    // 旋转：中键，或者 触摸板双指滑动
    else if (isMMB || (scrollX != 0 || scrollY != 0)) {
        intentOrbit = true;
    }

    // =========================================================
    // 状态更新 (用于控制 Gizmo 显示等)
    // =========================================================
    if (intentPan || intentZoom || intentOrbit)
    {
        _isControlling = true;
    }
    else
    {
        // 如果没有按键，且不是在惯性滑动中（这里简单用按键判断），则标记结束
        if (!isLMB && !isMMB && !isRMB && scrollX == 0 && scrollY == 0) 
        {
            _isControlling = false;
        }
    }

    // =========================================================
    // 执行逻辑
    // =========================================================

    // --- 1. 平移 (Pan) ---
    if (intentPan)
    {
        float sens = 0.002f * _currentOrbitDist;
        glm::vec3 delta(0.0f);
        glm::vec3 right = cam->transform.getRight();
        glm::vec3 up = cam->transform.getUp();

        if (isMMB) { 
            // 鼠标中键拖拽
            delta = (right * -dx * sens) + (up * dy * sens);
        } else {     
            // 触摸板滑动
            float trackpadSens = 5.0f * sens; 
            delta = (right * -scrollX * trackpadSens) + (up * scrollY * trackpadSens);
        }
        
        // 应用平移：相机位置和 Pivot 都要动
        cam->transform.position += delta;
        _pivot += delta; 
    }
    
    // --- 2. 缩放 (Zoom) ---
    else if (intentZoom)
    {
        float zoomFactor = 1.0f;
        float inputVal = scrollY != 0 ? scrollY : scrollX;

        if (isPhysicalMouse) {
            // 物理滚轮：固定步进 (10%)
            zoomFactor = (inputVal > 0) ? 0.9f : 1.1f;
        } else {
            // 触控板捏合：线性平滑缩放
            float safeInput = glm::clamp(inputVal, -2.0f, 2.0f);
            zoomFactor = 1.0f - (safeInput * 0.3f); 
        }

        // 修改目标距离，updateSmoothZoom 会负责插值
        _targetOrbitDist *= zoomFactor;
        if (_targetOrbitDist < 0.1f) _targetOrbitDist = 0.1f;
    }

    // --- 3. 旋转 (Orbit) ---
    else if (intentOrbit)
    {
        float targetDeltaX = 0.0f;
        float targetDeltaY = 0.0f;

        if (isMMB) {
            // 鼠标中键：直接映射
            float mouseSens = 0.0015f; // 可以微调灵敏度
            targetDeltaX = -dx * mouseSens;
            targetDeltaY = -dy * mouseSens;
            
            // 鼠标模式下，直接应用，不使用惯性变量干扰
            // (或者你可以选择让鼠标也有惯性，看手感喜好)
        } else {
            // 触控板：需要惯性平滑
            float trackpadScaleX = 0.15f;
            float trackpadScaleY = 0.12f;
            targetDeltaX = -scrollX * trackpadScaleX;
            targetDeltaY = -scrollY * trackpadScaleY;
            
            // 更新惯性变量
            _smoothOrbitDelta.x = glm::mix(_smoothOrbitDelta.x, targetDeltaX, dt * friction);
            _smoothOrbitDelta.y = glm::mix(_smoothOrbitDelta.y, targetDeltaY, dt * friction);
        }

        // 决定最终的旋转量
        float activeDx = isMMB ? targetDeltaX : _smoothOrbitDelta.x;
        float activeDy = isMMB ? targetDeltaY : _smoothOrbitDelta.y;

        rotateCamera(activeDx, activeDy);
    }
    
    // --- 4. 惯性衰减 ---
    // 即使没有输入，惯性也需要慢慢停下来
    else {
        _smoothOrbitDelta = glm::mix(_smoothOrbitDelta, glm::vec2(0.0f), dt * 30.0f);
        
        // 如果还有残余惯性，继续旋转一点点
        if (glm::length(_smoothOrbitDelta) > 0.001f) {
             rotateCamera(_smoothOrbitDelta.x, _smoothOrbitDelta.y);
             _isControlling = true; // 只要还在转，就算 controlling
        }
    }
}

// ... 依次搬运 rotateCamera, frameObject, updateAnimation 等函数 ...
Ray EditorCamera::screenPointToRay(float mouseX, float mouseY, float viewportX, float viewportY, float viewportW, float viewportH)
{
    // 1. 安全检查
    if (viewportW <= 0 || viewportH <= 0) 
        return Ray(glm::vec3(0), glm::vec3(0,0,1));

    // 2. [新增] 计算局部坐标 (Local Space)
    // 鼠标在整个窗口的坐标 - 视口图片左上角的坐标 = 鼠标在视口内的坐标
    float localX = mouseX - viewportX;
    float localY = mouseY - viewportY;

    // 3. 归一化设备坐标 (NDC: -1 ~ 1)
    float x = (2.0f * localX) / viewportW - 1.0f;
    float y = 1.0f - (2.0f * localY) / viewportH; // OpenGL Y轴向上，屏幕坐标向下，需要翻转

    // 4. 获取当前相机矩阵
    // 注意：这里访问的是 EditorCamera 自己的成员 _cameras
    Camera* cam = _cameras[_activeCameraIndex].get();
    glm::mat4 proj = cam->getProjectionMatrix();
    glm::mat4 view = cam->getViewMatrix();
    
    // 5. 反投影 (Unproject)
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 screenPos = glm::vec4(x, y, 1.0f, 1.0f);
    glm::vec4 worldPos = invVP * screenPos;

    if (worldPos.w != 0.0f) worldPos /= worldPos.w;

    // 6. 计算方向
    glm::vec3 dir = glm::normalize(glm::vec3(worldPos) - cam->transform.position);

    return Ray(cam->transform.position, dir);
}

// =======================================================================================
// 动画与平滑逻辑
// =======================================================================================

// [搬运来源] SceneRoaming::updateSmoothZoom
// [改动] 参数改为传入 deltaTime，不再依赖 ImGui::GetIO()
void EditorCamera::updateSmoothZoom(float dt)
{
    if (_isAnimating) return;

    float smoothFactor = 10.0f * dt;
    
    if (std::abs(_targetOrbitDist - _currentOrbitDist) < 0.01f) {
        _currentOrbitDist = _targetOrbitDist;
    } else {
        _currentOrbitDist = glm::mix(_currentOrbitDist, _targetOrbitDist, smoothFactor);
    }

    // 根据新的距离更新相机位置
    glm::vec3 dir = glm::normalize(_cameras[_activeCameraIndex]->transform.position - _pivot);
    _cameras[_activeCameraIndex]->transform.position = _pivot + dir * _currentOrbitDist;
}

// [搬运来源] SceneRoaming::startCameraAnimation
// [改动] 变量名 _cameraPivot -> _pivot, activeCameraIndex -> _activeCameraIndex
void EditorCamera::startAnimation(const glm::vec3& targetPos, const glm::quat& targetRot, const glm::vec3& targetPivot)
{
    _animStartPos = _cameras[_activeCameraIndex]->transform.position;
    _animStartPivot = _pivot;
    _animStartRot = _cameras[_activeCameraIndex]->transform.rotation;

    _animTargetPos = targetPos;
    _animTargetPivot = targetPivot;
    _animTargetRot = targetRot;

    // 最短路径检查
    if (glm::dot(_animStartRot, _animTargetRot) < 0.0f)
    {
        _animTargetRot = -_animTargetRot;
    }

    _targetOrbitDist = glm::length(targetPos - targetPivot);

    _animTime = 0.0f;
    _isAnimating = true;
}

// [搬运来源] SceneRoaming::updateCameraAnimation
void EditorCamera::updateAnimation(float dt)
{
    if (!_isAnimating) return;

    _animTime += dt;
    float t = _animTime / _animDuration;
    
    if (t >= 1.0f) {
        t = 1.0f;
        _isAnimating = false;
        // 强制吸附
        _pivot = _animTargetPivot;
        _currentOrbitDist = _targetOrbitDist;
        _cameras[_activeCameraIndex]->transform.position = _animTargetPos;
        _cameras[_activeCameraIndex]->transform.rotation = _animTargetRot;
        return;
    }

    float smoothT = 1.0f - pow(1.0f - t, 4.0f);

    // 插值逻辑
    glm::vec3 currentPivot = glm::mix(_animStartPivot, _animTargetPivot, smoothT);
    _pivot = currentPivot; 

    float startDist = glm::length(_animStartPos - _animStartPivot);
    float targetDist = glm::length(_animTargetPos - _animTargetPivot);
    float currentDist = glm::mix(startDist, targetDist, smoothT);
    _currentOrbitDist = currentDist; 

    glm::quat currentRot = glm::slerp(_animStartRot, _animTargetRot, smoothT);

    glm::vec3 offset = currentRot * glm::vec3(0.0f, 0.0f, 1.0f) * currentDist;
    glm::vec3 currentPos = currentPivot + offset;

    _cameras[_activeCameraIndex]->transform.rotation = currentRot;
    _cameras[_activeCameraIndex]->transform.position = currentPos;
}

// =======================================================================================
// 控制逻辑
// =======================================================================================

// [搬运来源] SceneRoaming::rotateCamera
void EditorCamera::rotateCamera(float dx, float dy)
{
    if (glm::length(glm::vec2(dx, dy)) < 0.00001f) return;

    Camera* cam = _cameras[_activeCameraIndex].get();

    glm::vec3 worldUp = glm::vec3(0, 1, 0);
    glm::vec3 camRight = cam->transform.getRight();

    glm::quat qYaw = glm::angleAxis(dx, worldUp);
    glm::quat qPitch = glm::angleAxis(dy, camRight);
    glm::quat qRotation = qYaw * qPitch;

    glm::vec3 pivotToCam = cam->transform.position - _pivot;
    pivotToCam = qRotation * pivotToCam; 
    cam->transform.position = _pivot + pivotToCam;

    cam->transform.rotation = qRotation * cam->transform.rotation;
    cam->transform.rotation = glm::normalize(cam->transform.rotation);
}

// [搬运来源] SceneRoaming::switchToView
void EditorCamera::switchToView(const glm::vec3& dir)
{
    glm::vec3 targetPos = _pivot + glm::normalize(dir) * _targetOrbitDist; 
    
    glm::vec3 up = glm::vec3(0, 1, 0);
    if (std::abs(dir.y) > 0.9f) {
        up = glm::vec3(0, 0, -1);
    }

    glm::mat4 targetViewMat = glm::lookAt(targetPos, _pivot, up);
    glm::quat targetRot = glm::quat_cast(glm::inverse(targetViewMat));

    startAnimation(targetPos, targetRot, _pivot);
}

// [搬运来源] SceneRoaming::frameSelectedObject
// [改动] 参数改为 GameObject* obj
void EditorCamera::frameObject(GameObject* obj)
{
    if (!obj) return;

    BoundingBox bounds;
    // bool hasBounds = false; // 未使用
    glm::vec3 centerOffset(0.0f); 
    float objectRadius = 1.0f;    

    if (auto mesh = obj->getComponent<MeshComponent>()) {
        bounds = mesh->model->getBoundingBox();
        // hasBounds = true;

        glm::vec3 localCenter = (bounds.min + bounds.max) * 0.5f;
        centerOffset = obj->transform.rotation * (localCenter * obj->transform.scale);

        glm::vec3 size = (bounds.max - bounds.min) * obj->transform.scale;
        objectRadius = glm::length(size) * 0.5f; 
    }

    glm::vec3 targetPivot = obj->transform.position + centerOffset;

    if (objectRadius < 0.5f) objectRadius = 0.5f;
    
    float halfFov = glm::radians(30.0f);
    float dist = objectRadius / glm::sin(halfFov);
    dist *= 1.3f; 

    _targetOrbitDist = dist; 

    glm::vec3 fixedDir = glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f));
    glm::vec3 targetPos = targetPivot + fixedDir * dist;

    glm::vec3 targetUp = glm::vec3(0, 1, 0);
    glm::mat4 targetView = glm::lookAt(targetPos, targetPivot, targetUp);
    glm::quat targetRot = glm::quat_cast(glm::inverse(targetView));

    startAnimation(targetPos, targetRot, targetPivot);
}

// =======================================================================================
// Gizmo 绘制与交互
// =======================================================================================

// [搬运来源] SceneRoaming::renderUI 中 "{ // [新增] 绘制 View Gizmo ... }" 代码块
// [改动] 封装了原本在 renderUI 里的交互逻辑
bool EditorCamera::drawViewGizmo(const glm::vec2& viewportPos, const glm::vec2& viewportSize)
{
    float gizmoSize = 65.0f; 
    float safePadding = gizmoSize + 15.0f + 30.0f;

    ImVec2 gizmoCenter = ImVec2(
        viewportPos.x + viewportSize.x - safePadding,
        viewportPos.y + safePadding
    );

    glm::mat4 view = _cameras[_activeCameraIndex]->getViewMatrix();
    bool isGizmoHovered = false;

    // 调用内部绘制函数 (对应旧的 drawViewGizmo)
    glm::vec3 clickedDir = drawGizmoInternal(
        ImGui::GetWindowDrawList(), 
        glm::vec2(gizmoCenter.x, gizmoCenter.y), 
        gizmoSize,
        isGizmoHovered
    );

    // --- 拖拽逻辑 ---
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && isGizmoHovered && glm::length(clickedDir) < 0.1f)
    {
        _isGizmoDragging = true;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        _isGizmoDragging = false;
    }

    if (_isGizmoDragging)
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        float sens = 0.005f; 
        rotateCamera(-delta.x * sens, -delta.y * sens);
    }

    // --- 点击吸附逻辑 (Snap View) ---
    if (!_isGizmoDragging && glm::length(clickedDir) > 0.1f)
    {
        float dist = glm::length(_cameras[_activeCameraIndex]->transform.position - _pivot);
        if (dist < 1.0f) dist = 5.0f;

        glm::vec3 targetPos = _pivot + clickedDir * dist;
        glm::vec3 up = glm::vec3(0, 1, 0); 
        glm::vec3 currentDir = glm::normalize(_cameras[_activeCameraIndex]->transform.position - _pivot);
        glm::vec3 currentUp = _cameras[_activeCameraIndex]->transform.getUp();

        // 逻辑完全搬运自 SceneRoaming
        if (abs(clickedDir.y) > 0.9f) {
            float invert = (currentUp.y < -0.1f) ? -1.0f : 1.0f;
            if (abs(currentDir.z) > abs(currentDir.x)) {
                float sign = (currentDir.z >= 0.0f) ? 1.0f : -1.0f;
                if (clickedDir.y > 0) up = glm::vec3(0, 0, -1.0f * sign * invert);
                else                  up = glm::vec3(0, 0, 1.0f * sign * invert);
            } else {
                float sign = (currentDir.x >= 0.0f) ? 1.0f : -1.0f;
                if (clickedDir.y > 0) up = glm::vec3(-1.0f * sign * invert, 0, 0);
                else                  up = glm::vec3(1.0f * sign * invert, 0, 0);
            }
        }
        else {
            float dot = glm::dot(clickedDir, currentUp);
            bool isBackFlip  = dot > 0.5f;   
            bool isFrontFlip = dot < -0.5f;  
            bool isTopHemi    = currentDir.y > 0.1f;  
            bool isAlreadyUpsideDown = currentUp.y < -0.1f;

            if (isBackFlip) {
                if (isTopHemi) up = glm::vec3(0, -1, 0); 
                else           up = glm::vec3(0, 1, 0);  
            }
            else if (isFrontFlip) {
                if (isTopHemi) up = glm::vec3(0, 1, 0);
                else           up = glm::vec3(0, -1, 0); 
            }
            else {
                if (isAlreadyUpsideDown) up = glm::vec3(0, -1, 0); 
                else                     up = glm::vec3(0, 1, 0);
            }
        }

        glm::mat4 targetViewMat = glm::lookAt(targetPos, _pivot, up); 
        glm::quat targetRot = glm::quat_cast(glm::inverse(targetViewMat));
        startAnimation(targetPos, targetRot, _pivot);
    }

    return isGizmoHovered;
}

// [搬运来源] SceneRoaming::drawViewGizmo (最原始的那个绘制函数)
// [改动] 参数简化，不再传入 cameraPos 等，因为类成员里有
glm::vec3 EditorCamera::drawGizmoInternal(ImDrawList* drawList, const glm::vec2& centerPos, float axisLength, bool& outGizmoHovered)
{
    ImVec2 center(centerPos.x, centerPos.y);

    float circleRadius = 15.0f;    
    float lineThickness = 4.0f;   
    float outlineThickness = 3.0f; 
    float fontSize = 23.0f;
    float bgRadius = axisLength + circleRadius * 2.0f;

    ImU32 colR = IM_COL32(240, 55, 82, 255);
    ImU32 colG = IM_COL32(110, 159, 29, 255);
    ImU32 colB = IM_COL32(47, 132, 229, 255);
    ImU32 colR_Trans = IM_COL32(240, 55, 82, 100);
    ImU32 colG_Trans = IM_COL32(110, 159, 29, 100);
    ImU32 colB_Trans = IM_COL32(47, 132, 229, 100);
    ImU32 colText = IM_COL32(0, 0, 0, 255);
    ImU32 colBgHover = IM_COL32(255, 255, 255, 30); 

    ImVec2 mousePos = ImGui::GetMousePos();
    float distFromCenter = sqrtf(powf(mousePos.x - center.x, 2) + powf(mousePos.y - center.y, 2));
    outGizmoHovered = (distFromCenter < bgRadius);

    if (outGizmoHovered || _isGizmoDragging) {
        drawList->AddCircleFilled(center, bgRadius, colBgHover);
    }

    std::vector<GizmoAxisData> axes = {
        { {1,0,0},  colR, 0,          'X', false },
        { {0,1,0},  colG, 0,          'Y', false },
        { {0,0,1},  colB, 0,          'Z', false },
        { {-1,0,0}, colR, colR_Trans, 0,   true },
        { {0,-1,0}, colG, colG_Trans, 0,   true },
        { {0,0,-1}, colB, colB_Trans, 0,   true }
    };

    glm::mat4 viewMatrix = _cameras[_activeCameraIndex]->getViewMatrix();
    glm::mat3 viewRot = glm::mat3(viewMatrix);
    
    for (auto& axis : axes) {
        glm::vec3 localDir = viewRot * axis.dir;
        axis.zDepth = localDir.z;
        axis.screenPos = ImVec2(
            center.x + localDir.x * axisLength,
            center.y - localDir.y * axisLength
        );
    }

    std::sort(axes.begin(), axes.end(), [](const GizmoAxisData& a, const GizmoAxisData& b) {
        return a.zDepth < b.zDepth;
    });

    bool isMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const GizmoAxisData* hoveredAxis = nullptr;

    ImFont* font = ImGui::GetFont();
    for (const auto& axis : axes)
    {
        float dist = sqrtf(powf(mousePos.x - axis.screenPos.x, 2) + powf(mousePos.y - axis.screenPos.y, 2));
        if (dist <= circleRadius + 2.0f) hoveredAxis = &axis;
        bool isHovered = (hoveredAxis == &axis);
        if (_isGizmoDragging) isHovered = false;

        if (!axis.isNegative)
        {
            if (isHovered) drawList->AddCircle(axis.screenPos, circleRadius + 2.0f, IM_COL32(255,255,255,150), 0, 2.0f);
            glm::vec2 dir2D = glm::vec2(axis.screenPos.x - center.x, axis.screenPos.y - center.y);
            float len = glm::length(dir2D);
            if (len > circleRadius) 
            {
                dir2D /= len;
                ImVec2 lineEndPos = ImVec2(
                    axis.screenPos.x - dir2D.x * (circleRadius - 1.5f), 
                    axis.screenPos.y - dir2D.y * (circleRadius - 1.5f)
                );
                drawList->AddLine(center, lineEndPos, axis.mainColor, lineThickness);
            }
            drawList->AddCircleFilled(axis.screenPos, circleRadius - 1.0f, axis.mainColor);
            drawList->AddCircle(axis.screenPos, circleRadius, axis.mainColor, 0, outlineThickness);

            char text[2] = { axis.label, '\0' };
            ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
            ImVec2 opticalOffset = ImVec2(0.4f, 0.4f); 
            ImVec2 textPos = ImVec2(
                axis.screenPos.x - textSize.x * 0.5f + opticalOffset.x,
                axis.screenPos.y - textSize.y * 0.5f + opticalOffset.y
            );
            drawList->AddText(font, fontSize, textPos, colText, text);
        }
        else
        {
            if (isHovered) drawList->AddCircle(axis.screenPos, circleRadius + 2.0f, IM_COL32(255,255,255,150), 0, 2.0f);
            drawList->AddCircleFilled(axis.screenPos, circleRadius - 1.0f, axis.fillColor);
            drawList->AddCircle(axis.screenPos, circleRadius, axis.mainColor, 0, outlineThickness);
        }
    }

    if (_isGizmoDragging) return glm::vec3(0,0,0);
    if (isMouseClicked && hoveredAxis) return hoveredAxis->dir;

    return glm::vec3(0, 0, 0); 
}