#pragma once

#include <glad/gl.h>
#include <vector>
#include <memory>
#include <iostream>

#include "base/glsl_program.h"
#include "base/camera.h"
#include "engine/model.h"
#include "scene_object.h" // 为了访问 GameObject

class OutlinePass
{
public:
    OutlinePass(int screenWidth, int screenHeight);
    ~OutlinePass();

    // 当窗口大小改变时调用
    void onResize(int width, int height);

    // 核心渲染函数
    // 1. targetObj: 当前选中的物体
    // 2. camera: 当前相机 (需要视图和投影矩阵)
    void render(GameObject *targetObj, Camera *camera, float contentScale, int width, int height);

private:
    int _screenWidth, _screenHeight;

    // FBO 相关
    GLuint _fbo = 0;
    GLuint _maskTexture = 0;       // 存储黑白遮罩
    // GLuint _depthRenderBuffer = 0; // 深度缓冲 (即使是遮罩也需要深度测试来保证遮挡关系正确吗？通常做Outline时，我们希望Outline被前面的物体遮挡，所以需要深度)

    // [新增] MSAA FBO (用于渲染 Mask)
    GLuint _msaaFbo = 0;
    GLuint _msaaColorBuffer = 0; // 使用 RenderBuffer 而不是 Texture，因为不需要采样
    GLuint _msaaDepthBuffer = 0; // MSAA 深度缓冲

    // 全屏矩形资源
    GLuint _quadVAO = 0;
    GLuint _quadVBO = 0;

    // Shaders
    std::unique_ptr<GLSLProgram> _maskShader; // 用于把物体画成纯白
    std::unique_ptr<GLSLProgram> _postShader; // 用于边缘检测和混合

    void initFrameBuffer();
    void initQuad();
    void initShaders();
};