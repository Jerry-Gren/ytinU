#include "model.h"
#include "obj_loader.h"

#include <algorithm>
#include <iostream>
#include <limits>

Model::Model(const std::string &filepath, bool useFlatShade)
    : _isUploaded(false)
{
    // 1. 调用 OBJLoader 获取数据
    // 这里利用了 C++ 的返回值优化 (RVO)，不会产生不必要的深拷贝
    MeshData data = OBJLoader::load(filepath, useFlatShade);

    // 2. 将数据移动到 Model 的成员变量中
    _vertices = std::move(data.vertices);
    _indices = std::move(data.indices);

    // 3. 后续初始化流程保持不变
    computeBoundingBox();
}

Model::Model(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices)
    : _vertices(vertices), _indices(indices), _isUploaded(false)
{
    computeBoundingBox();
}

Model::Model(Model &&rhs) noexcept
    : _vertices(std::move(rhs._vertices)), _indices(std::move(rhs._indices)),
      _boundingBox(std::move(rhs._boundingBox)), _vao(rhs._vao), _vbo(rhs._vbo), _ebo(rhs._ebo),
      _boxVao(rhs._boxVao), _boxVbo(rhs._boxVbo), _boxEbo(rhs._boxEbo)
{
    rhs._vao = 0;
    rhs._vbo = 0;
    rhs._ebo = 0;

    rhs._boxVao = 0;
    rhs._boxVbo = 0;
    rhs._boxEbo = 0;
}

Model::~Model()
{
    cleanup();
}

BoundingBox Model::getBoundingBox() const
{
    return _boundingBox;
}

void Model::initGL()
{
    if (_isUploaded) return; // 防止重复初始化

    // 确保此时有 OpenGL 上下文 (如果没有，glGetError 或 glGen* 会报错/崩溃，但此时通常都在渲染循环里了)
    initGLResources();
    initBoxGLResources();

    _isUploaded = true;
}

void Model::draw()
{
    if (!_isUploaded) {
        // const_cast 是一种妥协，或者将 initGL 声明为 const 并把内部变量设为 mutable
        // 这里最优雅的方式是将 _isUploaded 设为 mutable (已在 .h 中完成)
        const_cast<Model*>(this)->initGL();
    }

    if (_vao == 0) return; // 如果初始化失败，防止崩溃

    glBindVertexArray(_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Model::drawBoundingBox()
{
    if (!_isUploaded) {
         const_cast<Model*>(this)->initGL();
    }

    if (_boxVao == 0) return;
    
    glBindVertexArray(_boxVao);
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

GLuint Model::getVao() const
{
    return _vao;
}

GLuint Model::getBoundingBoxVao() const
{
    return _boxVao;
}

size_t Model::getVertexCount() const
{
    return _vertices.size();
}

size_t Model::getFaceCount() const
{
    return _indices.size() / 3;
}

void Model::initGLResources()
{
    // create a vertex array object
    glGenVertexArrays(1, &_vao);
    // create a vertex buffer object
    glGenBuffers(1, &_vbo);
    // create a element array buffer
    glGenBuffers(1, &_ebo);

    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(
        GL_ARRAY_BUFFER, sizeof(Vertex) * _vertices.size(), _vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER, _indices.size() * sizeof(uint32_t), _indices.data(),
        GL_STATIC_DRAW);

    // specify layout, size of a vertex, data type, normalize, sizeof vertex array, offset of the
    // attribute
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoord));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Model::computeBoundingBox()
{
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();

    for (const auto &v : _vertices)
    {
        minX = std::min(v.position.x, minX);
        minY = std::min(v.position.y, minY);
        minZ = std::min(v.position.z, minZ);
        maxX = std::max(v.position.x, maxX);
        maxY = std::max(v.position.y, maxY);
        maxZ = std::max(v.position.z, maxZ);
    }

    _boundingBox.min = glm::vec3(minX, minY, minZ);
    _boundingBox.max = glm::vec3(maxX, maxY, maxZ);

    // =========================================================
    // [修复] 防止零厚度导致的射线检测失败
    // 给极薄的物体（如 Plane, Quad）增加一个微小的厚度 (Epsilon)
    // =========================================================
    constexpr float EPSILON = 0.01f;

    if ((_boundingBox.max.x - _boundingBox.min.x) < EPSILON)
    {
        _boundingBox.max.x += EPSILON;
        _boundingBox.min.x -= EPSILON;
    }
    if ((_boundingBox.max.y - _boundingBox.min.y) < EPSILON)
    {
        _boundingBox.max.y += EPSILON;
        _boundingBox.min.y -= EPSILON; // 向下加厚一点
    }
    if ((_boundingBox.max.z - _boundingBox.min.z) < EPSILON)
    {
        _boundingBox.max.z += EPSILON;
        _boundingBox.min.z -= EPSILON;
    }
}

void Model::initBoxGLResources()
{
    std::vector<glm::vec3> boxVertices = {
        glm::vec3(_boundingBox.min.x, _boundingBox.min.y, _boundingBox.min.z),
        glm::vec3(_boundingBox.max.x, _boundingBox.min.y, _boundingBox.min.z),
        glm::vec3(_boundingBox.min.x, _boundingBox.max.y, _boundingBox.min.z),
        glm::vec3(_boundingBox.max.x, _boundingBox.max.y, _boundingBox.min.z),
        glm::vec3(_boundingBox.min.x, _boundingBox.min.y, _boundingBox.max.z),
        glm::vec3(_boundingBox.max.x, _boundingBox.min.y, _boundingBox.max.z),
        glm::vec3(_boundingBox.min.x, _boundingBox.max.y, _boundingBox.max.z),
        glm::vec3(_boundingBox.max.x, _boundingBox.max.y, _boundingBox.max.z),
    };

    std::vector<uint32_t> boxIndices = {0, 1, 0, 2, 0, 4, 3, 1, 3, 2, 3, 7,
                                        5, 4, 5, 1, 5, 7, 6, 4, 6, 7, 6, 2};

    glGenVertexArrays(1, &_boxVao);
    glGenBuffers(1, &_boxVbo);
    glGenBuffers(1, &_boxEbo);

    glBindVertexArray(_boxVao);
    glBindBuffer(GL_ARRAY_BUFFER, _boxVbo);
    glBufferData(
        GL_ARRAY_BUFFER, boxVertices.size() * sizeof(glm::vec3), boxVertices.data(),
        GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _boxEbo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER, boxIndices.size() * sizeof(uint32_t), boxIndices.data(),
        GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), 0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void Model::cleanup()
{
    if (_boxEbo)
    {
        glDeleteBuffers(1, &_boxEbo);
        _boxEbo = 0;
    }

    if (_boxVbo)
    {
        glDeleteBuffers(1, &_boxVbo);
        _boxVbo = 0;
    }

    if (_boxVao)
    {
        glDeleteVertexArrays(1, &_boxVao);
        _boxVao = 0;
    }

    if (_ebo != 0)
    {
        glDeleteBuffers(1, &_ebo);
        _ebo = 0;
    }

    if (_vbo != 0)
    {
        glDeleteBuffers(1, &_vbo);
        _vbo = 0;
    }

    if (_vao != 0)
    {
        glDeleteVertexArrays(1, &_vao);
        _vao = 0;
    }
}
