#pragma once

#include <glm/glm.hpp>
#include "scene.h"
#include "base/camera.h"
#include "scene_object.h"

// 前置声明，避免循环引用
class Renderer; 

class PlanarReflectionPass
{
public:
    PlanarReflectionPass() = default;
    ~PlanarReflectionPass() = default;

    // 核心渲染函数
    // mirrorObj: 挂载了 PlanarReflectionComponent 的那个物体
    // mainCamera: 当前的主摄像机
    // renderer: 用于回调绘制函数
    void render(const Scene& scene, GameObject* mirrorObj, Camera* mainCamera, Renderer* renderer);

private:
    // --- 数学辅助函数 ---

    // 计算关于平面镜面对称的 View 矩阵
    glm::mat4 computeReflectionViewMatrix(Camera* mainCam, const glm::vec3& planePos, const glm::vec3& planeNormal);

    // 计算斜视锥投影矩阵 (Oblique Frustum Clipping)
    // 作用：修改近裁剪面，使其与镜面对齐，从而剔除镜子背后的物体
    glm::mat4 computeObliqueProjection(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& planePos, const glm::vec3& planeNormal);
    
    // 简单的符号函数
    float sgn(float a) {
        if (a > 0.0f) return 1.0f;
        if (a < 0.0f) return -1.0f;
        return 0.0f;
    }
};