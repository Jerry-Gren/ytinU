#include "point_shadow_pass.h"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

PointShadowPass::PointShadowPass(int resolution, int maxLights)
    : _resolution(resolution), _maxLights(maxLights)
{
    initShader();
    initResources();
}

PointShadowPass::~PointShadowPass()
{
    for (const auto& buf : _shadowBuffers) {
        if (buf.fbo) glDeleteFramebuffers(1, &buf.fbo);
        if (buf.texture) glDeleteTextures(1, &buf.texture);
    }
}

void PointShadowPass::initResources()
{
    _shadowBuffers.resize(_maxLights);

    for (int i = 0; i < _maxLights; ++i) {
        // 1. 创建 Cubemap
        glGenTextures(1, &_shadowBuffers[i].texture);
        glBindTexture(GL_TEXTURE_CUBE_MAP, _shadowBuffers[i].texture);

        for (unsigned int face = 0; face < 6; ++face) {
            // 使用 GL_FLOAT 存储线性深度
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_DEPTH_COMPONENT24, 
                         _resolution, _resolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        // 2. 创建 FBO
        glGenFramebuffers(1, &_shadowBuffers[i].fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, _shadowBuffers[i].fbo);
        
        // 将 Cubemap 绑定为 Depth Attachment
        // 注意：如果是颜色纹理，通常需要用 Geometry Shader 动态分发
        // 但对于深度纹理，glFramebufferTexture 允许我们将整个 Cubemap 绑上去
        // 并在 Geometry Shader 中通过 gl_Layer 控制写入哪一面
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, _shadowBuffers[i].texture, 0);
        
        // 不需要颜色缓冲
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "ERROR::PointShadowFBO[" << i << "]:: Framebuffer is not complete!" << std::endl;
            
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void PointShadowPass::initShader()
{
    // Vertex Shader: 转到世界空间
    const char* vsCode = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        uniform mat4 model;
        void main() {
            gl_Position = model * vec4(aPos, 1.0);
        }
    )";

    // [核心] Geometry Shader: 一次生成 6 个面
    const char* gsCode = R"(
        #version 330 core
        layout (triangles) in;
        layout (triangle_strip, max_vertices=18) out;

        uniform mat4 shadowMatrices[6]; // 6个方向的 ViewProj 矩阵

        out vec4 FragPos; // 传递给 FS 计算距离

        void main() {
            for(int face = 0; face < 6; ++face) {
                gl_Layer = face; // 指定输出到 Cubemap 的哪个面
                for(int i = 0; i < 3; ++i) {
                    FragPos = gl_in[i].gl_Position;
                    gl_Position = shadowMatrices[face] * FragPos;
                    EmitVertex();
                }
                EndPrimitive();
            }
        }
    )";

    // Fragment Shader: 写入线性深度
    const char* fsCode = R"(
        #version 330 core
        in vec4 FragPos;

        uniform vec3 lightPos;
        uniform float farPlane;

        void main() {
            float lightDistance = length(FragPos.xyz - lightPos);
            
            // 映射到 [0, 1] 范围
            lightDistance = lightDistance / farPlane;
            
            // 手动写入深度 (修改 gl_FragDepth)
            gl_FragDepth = lightDistance;
        }
    )";

    _shader.reset(new GLSLProgram);
    _shader->attachVertexShader(vsCode);
    _shader->attachGeometryShader(gsCode); // 别忘了这个
    _shader->attachFragmentShader(fsCode);
    _shader->link();
}

void PointShadowPass::render(const Scene& scene, const std::vector<PointShadowInfo>& lightInfos)
{
    _shader->use();
    
    // 渲染尺寸
    glViewport(0, 0, _resolution, _resolution);
    
    // 遍历每一个需要投射阴影的光源
    for (const auto& info : lightInfos)
    {
        if (info.lightIndex >= _maxLights) continue;

        // 1. 绑定对应的 FBO
        glBindFramebuffer(GL_FRAMEBUFFER, _shadowBuffers[info.lightIndex].fbo);
        glClear(GL_DEPTH_BUFFER_BIT);

        // 2. 准备 6 个方向的矩阵
        // 投影矩阵：90度 FOV，宽高比 1.0
        glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, info.farPlane);
        
        std::vector<glm::mat4> shadowTransforms;
        shadowTransforms.push_back(shadowProj * glm::lookAt(info.position, info.position + glm::vec3( 1.0,  0.0,  0.0), glm::vec3(0.0, -1.0,  0.0)));
        shadowTransforms.push_back(shadowProj * glm::lookAt(info.position, info.position + glm::vec3(-1.0,  0.0,  0.0), glm::vec3(0.0, -1.0,  0.0)));
        shadowTransforms.push_back(shadowProj * glm::lookAt(info.position, info.position + glm::vec3( 0.0,  1.0,  0.0), glm::vec3(0.0,  0.0,  1.0)));
        shadowTransforms.push_back(shadowProj * glm::lookAt(info.position, info.position + glm::vec3( 0.0, -1.0,  0.0), glm::vec3(0.0,  0.0, -1.0)));
        shadowTransforms.push_back(shadowProj * glm::lookAt(info.position, info.position + glm::vec3( 0.0,  0.0,  1.0), glm::vec3(0.0, -1.0,  0.0)));
        shadowTransforms.push_back(shadowProj * glm::lookAt(info.position, info.position + glm::vec3( 0.0,  0.0, -1.0), glm::vec3(0.0, -1.0,  0.0)));

        // 传递给 Shader
        for (int i = 0; i < 6; ++i) {
            _shader->setUniformMat4("shadowMatrices[" + std::to_string(i) + "]", shadowTransforms[i]);
        }
        _shader->setUniformFloat("farPlane", info.farPlane);
        _shader->setUniformVec3("lightPos", info.position);

        // 3. 绘制场景
        // 注意：这里我们简单地画所有物体。为了性能，可以做视锥剔除（但对于全向光源，剔除比较复杂）。
        for (const auto& go : scene.getGameObjects()) {
            auto meshComp = go->getComponent<MeshComponent>();
            if (!meshComp || !meshComp->enabled) continue;
            if (meshComp->isGizmo) continue; // Gizmo 不投射阴影

            // 这里不需要剔除背面，为了让阴影更准确（尤其是封闭物体），
            // 有时甚至可以剔除正面(GL_FRONT)来修复彼得潘现象，视具体效果而定。
            // 这里暂且不做特殊 Cull Face 设置，沿用默认或外部设置。
            
            glm::mat4 model = go->transform.getLocalMatrix();
            if (meshComp->model) {
                model = model * meshComp->model->transform.getLocalMatrix();
                _shader->setUniformMat4("model", model);
                meshComp->model->draw();
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint PointShadowPass::getShadowMap(int index) const {
    if (index >= 0 && index < _shadowBuffers.size()) {
        return _shadowBuffers[index].texture;
    }
    return 0;
}