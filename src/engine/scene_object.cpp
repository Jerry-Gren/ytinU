#include "scene_object.h"
#include <glad/gl.h> // 只有这里需要包含 OpenGL 头文件，净化了头文件
#include <iostream>

// ==========================================
// IDGenerator
// ==========================================
int IDGenerator::generate() {
    static std::atomic<int> counter{ 1 };
    return counter.fetch_add(1);
}

// ==========================================
// Component
// ==========================================
Component::Component() : _instanceId(IDGenerator::generate()) {}

// ==========================================
// MeshComponent
// ==========================================
MeshComponent::MeshComponent(std::shared_ptr<Model> m, bool gizmo)
    : model(m), isGizmo(gizmo) {}

void MeshComponent::setMesh(std::shared_ptr<Model> newModel) {
    if (newModel) model = newModel;
}

// ==========================================
// LightComponent
// ==========================================
LightComponent::LightComponent(LightType t) : type(t) {}

// ==========================================
// ReflectionProbeComponent
// ==========================================
ReflectionProbeComponent::~ReflectionProbeComponent() {
    if (textureID) glDeleteTextures(1, &textureID);
    if (fboID) glDeleteFramebuffers(1, &fboID);
    if (rboID) glDeleteRenderbuffers(1, &rboID);
}

void ReflectionProbeComponent::initGL() {
    if (textureID != 0) return; // 已初始化

    // 1. 创建 Cubemap 纹理
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
    for (unsigned int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, 
                     resolution, resolution, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // 2. 创建 FBO
    glGenFramebuffers(1, &fboID);
    glBindFramebuffer(GL_FRAMEBUFFER, fboID);

    // 3. 创建深度缓冲 (RBO)
    glGenRenderbuffers(1, &rboID);
    glBindRenderbuffer(GL_RENDERBUFFER, rboID);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, resolution, resolution);
    
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboID);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::ReflectionProbe:: Framebuffer is not complete!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ==========================================
// PlanarReflectionComponent
// ==========================================
PlanarReflectionComponent::~PlanarReflectionComponent() {
    if (textureID) glDeleteTextures(1, &textureID);
    if (fboID) glDeleteFramebuffers(1, &fboID);
    if (rboID) glDeleteRenderbuffers(1, &rboID);
}

void PlanarReflectionComponent::initGL() {
    if (textureID != 0) return; // 防止重复初始化

    // 1. 创建纹理 (Color Buffer)
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    // 使用 RGB16F 以支持 HDR (高光不被截断)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, resolution, resolution, 0, GL_RGB, GL_FLOAT, NULL);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 2. 创建 FBO
    glGenFramebuffers(1, &fboID);
    glBindFramebuffer(GL_FRAMEBUFFER, fboID);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID, 0);

    // 3. 创建 RBO (深度缓冲)
    glGenRenderbuffers(1, &rboID);
    glBindRenderbuffer(GL_RENDERBUFFER, rboID);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, resolution, resolution);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboID);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::PlanarReflection:: Framebuffer is not complete!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ==========================================
// GameObject
// ==========================================
GameObject::GameObject(const std::string &n) : name(n), _instanceId(IDGenerator::generate()) {}

void GameObject::removeComponent(Component *comp)
{
    components.erase(
        std::remove_if(components.begin(), components.end(),
                        [comp](const std::unique_ptr<Component> &p)
                        { return p.get() == comp; }),
        components.end());
}