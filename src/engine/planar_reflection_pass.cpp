#include "planar_reflection_pass.h"
#include "renderer.h"
#include <glad/gl.h>
#include <iostream>

void PlanarReflectionPass::render(const Scene& scene, GameObject* mirrorObj, Camera* mainCamera, Renderer* renderer)
{
    auto reflectionComp = mirrorObj->getComponent<PlanarReflectionComponent>();
    if (!reflectionComp) return;

    // 1. 确保资源已初始化
    reflectionComp->initGL();

    // 2. 获取平面信息 (位置和法线)
    // 假设镜面物体的局部坐标系的 +Y 轴就是镜面的法线
    glm::vec3 planePos = mirrorObj->transform.position;
    glm::vec3 planeNormal = glm::normalize(mirrorObj->transform.rotation * glm::vec3(0, 1, 0));

    // 3. 计算虚拟相机的 View 矩阵
    glm::mat4 reflectionView = computeReflectionViewMatrix(mainCamera, planePos, planeNormal);

    // 4. 计算斜视锥投影矩阵 (Clip Plane)
    // 加上用户设置的偏移量 clipOffset (防止 Z-Fighting)
    glm::vec3 offsetPos = planePos + planeNormal * reflectionComp->clipOffset;
    glm::mat4 reflectionProj = computeObliqueProjection(mainCamera->getProjectionMatrix(), reflectionView, offsetPos, planeNormal);

    // 5. 准备渲染环境
    glBindFramebuffer(GL_FRAMEBUFFER, reflectionComp->fboID);
    glViewport(0, 0, reflectionComp->resolution, reflectionComp->resolution);
    
    // 背景色可以设为天际线颜色或黑色
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 6. 关键：翻转面剔除
    // 因为镜像变换会改变顶点的缠绕顺序 (Winding Order)，逆时针变顺时针。
    // 如果不反转，我们会看到物体的内部。
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT); // 正常是 BACK，这里改为 FRONT

    // 7. 设置 Shader 全局变量
    GLSLProgram* shader = renderer->getMainShader();
    shader->use();
    shader->setUniformMat4("view", reflectionView);
    shader->setUniformMat4("projection", reflectionProj);
    
    // 计算反射后的相机位置 (用于高光计算)
    // 简单数学：V' = V - 2*(V.N)*N (这里略过严格推导，直接用 View 矩阵逆推)
    glm::vec3 camPos = glm::vec3(glm::inverse(reflectionView)[3]);
    shader->setUniformVec3("viewPos", camPos);

    // 8. 收集渲染队列 (复用 Renderer 的逻辑)
    // 这里我们简单粗暴地收集所有物体（除了镜子自己）
    // 实际项目中可能需要做视锥剔除
    std::vector<GameObject*> renderQueue;
    for(const auto& go : scene.getGameObjects()) {
        auto mesh = go->getComponent<MeshComponent>();
        if(mesh && mesh->enabled) {
            renderQueue.push_back(go.get());
        }
    }

    // 9. 调用 Renderer 绘制
    // 注意：这里我们暂不处理反射里的反射 (递归)，所以 activeProbe 传空或全局
    renderer->renderObjectList(renderQueue, scene, mirrorObj, nullptr, nullptr);

    // 10. 绘制天空盒
    // 注意：我们需要去掉 View 矩阵的位移，就像正常画天空盒一样
    glm::mat4 viewNoTrans = glm::mat4(glm::mat3(reflectionView));
    renderer->drawSkybox(viewNoTrans, reflectionProj, scene.getEnvironment());

    // 11. 恢复状态
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

glm::mat4 PlanarReflectionPass::computeReflectionViewMatrix(Camera* mainCam, const glm::vec3& planePos, const glm::vec3& planeNormal)
{
    // 计算反射矩阵核心逻辑
    // 1. 获取主相机的位置和方向
    glm::vec3 camPos = mainCam->transform.position;
    glm::vec3 camDir = mainCam->transform.rotation * glm::vec3(0, 0, -1);
    glm::vec3 camUp  = mainCam->transform.rotation * glm::vec3(0, 1, 0);

    // 2. 计算点到平面的距离 (signed distance)
    // d = dot(C - P, N)
    float dist = glm::dot(camPos - planePos, planeNormal);

    // 3. 计算反射后的相机位置
    // R = C - 2 * d * N
    glm::vec3 reflectPos = camPos - 2.0f * dist * planeNormal;

    // 4. 反射相机的方向向量
    // 方向向量反射公式: R = D - 2 * dot(D, N) * N
    glm::vec3 reflectDir = glm::reflect(camDir, planeNormal);
    glm::vec3 reflectUp  = glm::reflect(camUp, planeNormal);

    // 5. 构建 View 矩阵
    return glm::lookAt(reflectPos, reflectPos + reflectDir, reflectUp);
}

glm::mat4 PlanarReflectionPass::computeObliqueProjection(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& planePos, const glm::vec3& planeNormal)
{
    // Eric Lengyel 的 Oblique Frustum Clipping 算法实现
    // 目的是修改投影矩阵的近裁剪面，使其与镜像平面重合

    glm::mat4 obliqueProj = projection;

    // 1. 将平面变换到视空间 (View Space)
    // 平面方程: Ax + By + Cz + D = 0
    // normal 是 (A,B,C), D = -dot(normal, point)
    glm::vec4 viewSpacePlane = glm::transpose(glm::inverse(view)) * glm::vec4(planeNormal, -glm::dot(planeNormal, planePos));
    
    // 2. 计算 q 向量 (视锥体对角线上的一个点)
    glm::vec4 q;
    q.x = (sgn(viewSpacePlane.x) + projection[2][0]) / projection[0][0];
    q.y = (sgn(viewSpacePlane.y) + projection[2][1]) / projection[1][1];
    q.z = -1.0f;
    q.w = (1.0f + projection[2][2]) / projection[2][3];

    // 3. 计算缩放因子 c
    float dotVal = viewSpacePlane.x * q.x + viewSpacePlane.y * q.y + viewSpacePlane.z * q.z + viewSpacePlane.w * q.w;
    // 避免除零
    if (std::abs(dotVal) < 0.0001f) dotVal = 0.0001f;
    
    glm::vec4 c = viewSpacePlane * (2.0f / dotVal);

    // 4. 替换投影矩阵的第三行 (Z行)
    obliqueProj[0][2] = c.x;
    obliqueProj[1][2] = c.y;
    obliqueProj[2][2] = c.z + 1.0f;
    obliqueProj[3][2] = c.w;

    return obliqueProj;
}