#pragma once

#include <glad/gl.h>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "base/glsl_program.h"
#include "scene.h"
#include "base/camera.h"

struct ShadowCasterInfo {
    glm::vec3 direction;
    float shadowNormalBias;
    unsigned int cullFaceMode;
};

class ShadowMapPass
{
public:
    // resolution: 单张贴图分辨率
    // maxLights: 最大支持的平行光数量
    ShadowMapPass(int resolution = 4096, int maxLights = 4);
    ~ShadowMapPass();

    // 核心渲染函数：接收光源列表
    void render(const Scene& scene, const std::vector<ShadowCasterInfo>& casters, Camera* camera);

    GLuint getDepthMapArray() const { return _depthMap; }

    // 返回所有光源的所有级联矩阵 (展平的一维数组)
    // 布局: [Light0_Casc0, Light0_Casc1..., Light1_Casc0...]
    const std::vector<glm::mat4>& getLightSpaceMatrices() const { return _lightSpaceMatrices; }

    const std::vector<float>& getCascadeLevels() const { return _cascadeLevels; }
    int getCascadeCount() const { return (int)_cascadeLevels.size() + 1; } // +1 因为最后一层是 zFar

private:
    int _resolution;
    int _maxLights;
    int _layerCountPerLight; // cascadeLevels.size() + 1

    GLuint _fbo = 0;
    GLuint _depthMap = 0;
    
    // 存储所有光源的矩阵
    std::vector<glm::mat4> _lightSpaceMatrices;
    std::vector<float> _cascadeLevels; 
    
    std::unique_ptr<GLSLProgram> _depthShader;

    void initFBO();
    void initShader();

    std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);
    
    glm::mat4 getLightSpaceMatrix(const float nearPlane, const float farPlane, const glm::vec3& lightDir, Camera* camera);
};