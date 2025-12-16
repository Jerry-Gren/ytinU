#pragma once

#include <vector>
#include <string>
#include <memory>
#include <glad/gl.h>
#include "base/vertex.h"
#include "base/glsl_program.h"
#include "base/texture2d.h" // 假设你有这个类，如果没有可以先注释掉材质部分

class Mesh
{
public:
    // 网格数据
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // 简单的材质属性（后续可扩展为Material类）
    // std::shared_ptr<Texture2D> albedoMap;

public:
    Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices);
    ~Mesh();

    // 禁止拷贝，防止 OpenGL 句柄重复释放
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    // 允许移动 (Move Semantics)
    Mesh(Mesh &&other) noexcept;
    Mesh &operator=(Mesh &&other) noexcept;

    // 绘制函数
    void draw(const GLSLProgram &shader) const;

private:
    GLuint _vao = 0;
    GLuint _vbo = 0;
    GLuint _ebo = 0;

    void setupMesh();
};
