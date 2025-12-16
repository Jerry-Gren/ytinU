#pragma once

#include <memory>
#include <vector>
#include <glad/gl.h>

#include "scene.h"
#include "base/camera.h"
#include "base/glsl_program.h"
#include "outline_pass.h"
#include "geometry_factory.h"

class Renderer
{
public:
    Renderer();
    ~Renderer();

    // 初始化 Shader、天空盒、网格等资源
    void init();

    // 调整大小 (通知 OutlinePass 等)
    void onResize(int width, int height);

    // 核心渲染函数
    // targetFBO: 传入 0 渲染到屏幕，传入 FBO ID 渲染到纹理
    // selectedObj: 如果非空，则绘制描边 (Editor 模式用)
    void render(const Scene& scene, Camera* camera, 
                GLuint targetFBO, int width, int height,
                float contentScale, 
                GameObject* selectedObj = nullptr);

private:
    // --- Shader 资源 ---
    std::unique_ptr<GLSLProgram> _mainShader;
    std::unique_ptr<GLSLProgram> _gridShader;
    std::unique_ptr<GLSLProgram> _skyboxShader;

    // --- 全局模型 ---
    std::shared_ptr<Model> _gridPlane;
    std::shared_ptr<Model> _skyboxCube;

    // --- Render Pass ---
    std::unique_ptr<OutlinePass> _outlinePass;

    // --- 内部绘制函数 ---
    void drawSkybox(const glm::mat4& view, const glm::mat4& proj);
    void drawGrid(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& viewPos);
    void drawSceneObjects(const Scene& scene, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& viewPos);
};