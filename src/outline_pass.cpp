#include "outline_pass.h"
#include <vector>

OutlinePass::OutlinePass(int width, int height)
    : _screenWidth(width), _screenHeight(height)
{
    initShaders();
    initQuad();
    initFrameBuffer();
}

OutlinePass::~OutlinePass()
{
    if (_fbo)
        glDeleteFramebuffers(1, &_fbo);
    if (_maskTexture)
        glDeleteTextures(1, &_maskTexture);
    // if (_depthRenderBuffer)
    //     glDeleteRenderbuffers(1, &_depthRenderBuffer);
    // [新增] 释放 MSAA 资源
    if (_msaaFbo)
        glDeleteFramebuffers(1, &_msaaFbo);
    if (_msaaColorBuffer)
        glDeleteRenderbuffers(1, &_msaaColorBuffer);
    if (_msaaDepthBuffer)
        glDeleteRenderbuffers(1, &_msaaDepthBuffer);
    if (_quadVAO)
        glDeleteVertexArrays(1, &_quadVAO);
    if (_quadVBO)
        glDeleteBuffers(1, &_quadVBO);
}

void OutlinePass::onResize(int width, int height)
{
    _screenWidth = width;
    _screenHeight = height;
    // 重新生成 FBO (简单粗暴的方法是删了重建)
    if (_fbo)
        glDeleteFramebuffers(1, &_fbo);
    if (_maskTexture)
        glDeleteTextures(1, &_maskTexture);
    // if (_depthRenderBuffer)
    //     glDeleteRenderbuffers(1, &_depthRenderBuffer);
    // [新增]
    if (_msaaFbo)
        glDeleteFramebuffers(1, &_msaaFbo);
    if (_msaaColorBuffer)
        glDeleteRenderbuffers(1, &_msaaColorBuffer);
    if (_msaaDepthBuffer)
        glDeleteRenderbuffers(1, &_msaaDepthBuffer);
    
    initFrameBuffer();
}

void OutlinePass::initFrameBuffer()
{
    // ==========================================
    // 1. 创建 MSAA FBO (渲染目标)
    // ==========================================
    glGenFramebuffers(1, &_msaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _msaaFbo);

    // 创建多重采样颜色缓冲 (4 samples)
    glGenRenderbuffers(1, &_msaaColorBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _msaaColorBuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_R8, _screenWidth, _screenHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _msaaColorBuffer);

    // 创建多重采样深度缓冲 (必须匹配)
    glGenRenderbuffers(1, &_msaaDepthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _msaaDepthBuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, _screenWidth, _screenHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _msaaDepthBuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: MSAA Framebuffer is not complete!" << std::endl;

    // ==========================================
    // 2. 创建 Resolve FBO (读取目标 - 普通纹理)
    // ==========================================
    glGenFramebuffers(1, &_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);

    // 创建普通单通道纹理 (GL_LINEAR 很重要)
    glGenTextures(1, &_maskTexture);
    glBindTexture(GL_TEXTURE_2D, _maskTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, _screenWidth, _screenHeight, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // 线性过滤
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _maskTexture, 0);

    // 这个 FBO 不需要深度缓冲，因为我们只是要把 MSAA 的颜色 Blit 过来

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: Resolve Framebuffer is not complete!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OutlinePass::initQuad()
{
    // 标准的覆盖全屏的 NDC 坐标
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f, 1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
        1.0f, -1.0f, 1.0f, 0.0f,

        -1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, -1.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f};
    glGenVertexArrays(1, &_quadVAO);
    glGenBuffers(1, &_quadVBO);
    glBindVertexArray(_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, _quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
}

void OutlinePass::initShaders()
{
    // 1. Mask Shader: 将物体渲染为纯白色
    const char *maskVs = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        void main() {
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }
    )";
    const char *maskFs = R"(
        #version 330 core
        out vec4 FragColor; // 只写 GL_RED
        void main() {
            FragColor = vec4(1.0, 0.0, 0.0, 1.0); // R=1
        }
    )";
    _maskShader.reset(new GLSLProgram);
    _maskShader->attachVertexShader(maskVs);
    _maskShader->attachFragmentShader(maskFs);
    _maskShader->link();

    // 2. Post Shader: 边缘检测
    const char *postVs = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTexCoords;
        out vec2 TexCoords;
        void main() {
            gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0); 
            TexCoords = aTexCoords;
        }
    )";

    const char *postFs = R"(
        #version 330 core
        out vec4 FragColor;
        in vec2 TexCoords;

        uniform sampler2D maskTexture;
        uniform float outlineWidth; 
        uniform vec3 outlineColor; 

        void main() {
            vec2 texSize = textureSize(maskTexture, 0);
            vec2 px = 1.0 / texSize; 
            
            // 采样中心
            float center = texture(maskTexture, TexCoords).r;

            // [修复 1] 移除硬 Discard，改用透明度控制内边缘
            // 原代码: if (center > 0.8) discard; (导致内圈锯齿)
            // 新逻辑: 如果 center 很白(物体内部)，我们让 alpha 变 0；
            // 如果 center 是灰色(物体抗锯齿边缘)，我们让 alpha 慢慢变 0。
            // 这样轮廓线会平滑地“隐入”物体后面。
            float innerAlpha = 1.0 - smoothstep(0.5, 0.9, center);

            // 提前优化：如果完全在物体内部，就不需要做昂贵的搜索了
            if (innerAlpha <= 0.0) discard;

            // 2. 暴力搜索 + [修复 2] 亚像素精度补偿
            int radius = int(ceil(outlineWidth));
            float minDistance = 1000.0; 

            for (int x = -radius; x <= radius; x++) {
                for (int y = -radius; y <= radius; y++) {
                    
                    vec2 offset = vec2(x, y) * px;
                    float neighbor = texture(maskTexture, TexCoords + offset).r;

                    // 只要邻居不是全黑
                    if (neighbor > 0.01) {
                        float dist = length(vec2(x, y));

                        // [核心黑科技] 亚像素距离补偿
                        // 如果 neighbor 是 1.0 (全白)，说明边界在这个像素的更外侧，实际距离更近，减去 0.5
                        // 如果 neighbor 是 0.1 (很淡)，说明边界在这个像素的内侧，实际距离更远
                        // 公式：修正后的距离 = 像素中心距离 - (亮度 - 0.5)
                        // 这样生成的距离场会非常平滑，不再受像素网格限制
                        float subPixelCorrection = neighbor - 0.5;
                        dist -= subPixelCorrection;

                        minDistance = min(minDistance, dist);
                    }
                }
            }

            // 3. 渲染逻辑
            if (minDistance > outlineWidth) discard;

            // 4. 外边缘抗锯齿 (保持不变，这部分逻辑是对的)
            float outerAlpha = 1.0 - smoothstep(outlineWidth - 1.0, outlineWidth, minDistance);
            
            // 5. 最终 Alpha = 外边缘衰减 * 内边缘遮罩
            float finalAlpha = outerAlpha * innerAlpha;

            // 提升一点实心感 (Gamma校正)
            finalAlpha = pow(finalAlpha, 0.5);

            if (finalAlpha > 0.01) {
                FragColor = vec4(outlineColor, finalAlpha);
            } else {
                discard;
            }
        }
    )";
    _postShader.reset(new GLSLProgram);
    _postShader->attachVertexShader(postVs);
    _postShader->attachFragmentShader(postFs);
    _postShader->link();
}

void OutlinePass::render(GameObject *targetObj, Camera *camera, float contentScale, int width, int height)
{
    if (!targetObj)
        return;
    
    // [新增] 1. 保存进入函数时绑定的 FBO (即 _sceneFbo)
    GLint prevFbo;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);

    if (width != _screenWidth || height != _screenHeight)
    {
        // 调用你现有的 onResize 函数重建 FBO
        onResize(width, height);
        
        // 注意：onResize 里已经更新了 _screenWidth 和 _screenHeight，
        // 所以下一帧不会重复进入这里。
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    // ===========================================
    // Pass 1: 渲染到 MSAA FBO (生成高精度 Mask)
    // ===========================================
    glBindFramebuffer(GL_FRAMEBUFFER, _msaaFbo); // <--- 改为绑定 MSAA FBO
    glViewport(0, 0, _screenWidth, _screenHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    _maskShader->use();
    _maskShader->setUniformMat4("view", camera->getViewMatrix());
    _maskShader->setUniformMat4("projection", camera->getProjectionMatrix());

    // 遍历该物体的所有 Mesh 组件进行渲染
    for (const auto &comp : targetObj->components)
    {
        if (comp->getType() == ComponentType::MeshRenderer)
        {
            auto mesh = static_cast<MeshComponent *>(comp.get());
            if (!mesh->enabled)
                continue;
            // Gizmo 通常不画外框，跳过
            // if (mesh->isGizmo)
            //     continue;

            // 计算矩阵
            glm::mat4 modelMatrix = targetObj->transform.getLocalMatrix();
            modelMatrix = modelMatrix * mesh->model->transform.getLocalMatrix();
            _maskShader->setUniformMat4("model", modelMatrix);

			if (mesh->doubleSided) {
                glDisable(GL_CULL_FACE); // 允许绘制背面到 Mask
            }
            mesh->model->draw();
            if (mesh->doubleSided) {
                glEnable(GL_CULL_FACE); // 恢复背面剔除
            }
        }
    }

    // ===========================================
    // Pass 1.5: 将 MSAA Resolve 到普通纹理
    // ===========================================
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _msaaFbo); // 源：MSAA
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);     // 目标：Texture
    
    // 执行 Blit：硬件会自动混合 4 个采样点，生成平滑的边缘 (抗锯齿)
    glBlitFramebuffer(0, 0, _screenWidth, _screenHeight, 
                      0, 0, _screenWidth, _screenHeight, 
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // ===========================================
    // Pass 2: 边缘检测并叠加 (渲染到屏幕)
    // ===========================================
    // [修改] 2. 恢复之前保存的 FBO，而不是绑定 0
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    // 重要：我们需要在现有的画面上“叠加”外框，而不是覆盖
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST); // 后处理是一个 2D 贴图，不需要深度测试

    _postShader->use();
    _postShader->setUniformVec3("outlineColor", glm::vec3(1.0, 0.6, 0.0)); // 橙色
    _postShader->setUniformFloat("outlineWidth", 3.0f * contentScale);     // 线宽 3

    glBindVertexArray(_quadVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _maskTexture); // 绑定刚才生成的黑白图
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // 恢复状态
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}
