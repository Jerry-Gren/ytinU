#pragma once

#include <glad/gl.h>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "base/glsl_program.h"
#include "scene.h"
#include "base/camera.h"

class ShadowMapPass
{
public:
    // 分辨率越高，锯齿越少。4096 是现代 PC 的标准配置。
    ShadowMapPass(int resolution = 4096); 
    ~ShadowMapPass();

    // 核心渲染函数：从灯光视角渲染场景
    void render(const Scene& scene, const glm::vec3& lightDir, Camera* camera, float shadowNormalBias, unsigned int cullFaceMode);

    // 获取数据供主 Shader 使用
    GLuint getDepthMapArray() const { return _depthMap; } // 现在这是一个 Texture Array
    const std::vector<glm::mat4>& getLightSpaceMatrices() const { return _lightSpaceMatrices; }
    const std::vector<float>& getCascadeLevels() const { return _cascadeLevels; }
    int getCascadeCount() const { return _lightSpaceMatrices.size(); }

private:
    int _resolution;
    GLuint _fbo = 0;
    GLuint _depthMap = 0;
    
    // CSM 数据
    std::vector<glm::mat4> _lightSpaceMatrices;
    std::vector<float> _cascadeLevels; // 存储每一层的分割距离 (远平面)
    
    std::unique_ptr<GLSLProgram> _depthShader;

    void initFBO();
    void initShader();

    // 内部数学辅助：获取视锥体切片的 8 个角点
    std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);
    
    // 内部数学辅助：计算单个级联的光照矩阵
    glm::mat4 getLightSpaceMatrix(const float nearPlane, const float farPlane, const glm::vec3& lightDir, Camera* camera);
};