#pragma once

#include <glad/gl.h>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include "base/glsl_program.h"
#include "scene.h"

// 用于传递单个点光源的渲染信息
struct PointShadowInfo {
    glm::vec3 position;
    float farPlane; // 点光源的视锥最远距离 (通常设为 25.0f 或更大)
    int lightIndex; // 对应 pointShadowMaps 数组的第几个槽位
};

class PointShadowPass
{
public:
    // resolution: Cubemap 的分辨率 (通常 1024)
    // maxLights: 最大支持的点光源阴影数量 (例如 4)
    PointShadowPass(int resolution = 1024, int maxLights = 4);
    ~PointShadowPass();

    // 核心渲染函数
    void render(const Scene& scene, const std::vector<PointShadowInfo>& lightInfos);

    // 获取某个槽位的 Cubemap ID
    GLuint getShadowMap(int index) const;
    
    // 获取最大支持数量
    int getMaxLights() const { return _maxLights; }

private:
    int _resolution;
    int _maxLights;

    // 每个光源对应一个 FBO 和一个 Cubemap
    struct ShadowFrameBuffer {
        GLuint fbo = 0;
        GLuint texture = 0; // Cubemap
    };
    std::vector<ShadowFrameBuffer> _shadowBuffers;

    std::unique_ptr<GLSLProgram> _shader;

    void initResources();
    void initShader();
};