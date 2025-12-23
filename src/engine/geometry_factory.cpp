#include "geometry_factory.h"
#include <cmath>

// 辅助函数：添加四边形面 (由两个三角形组成)
static void addQuad(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices,
                    const Vertex &v0, const Vertex &v1, const Vertex &v2, const Vertex &v3)
{
    // 两个三角形: 0-1-2 和 0-2-3
    uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
    vertices.push_back(v0);
    vertices.push_back(v1);
    vertices.push_back(v2);
    vertices.push_back(v3);

    indices.push_back(baseIndex + 0);
    indices.push_back(baseIndex + 1);
    indices.push_back(baseIndex + 2);

    indices.push_back(baseIndex + 0);
    indices.push_back(baseIndex + 2);
    indices.push_back(baseIndex + 3);
}

void GeometryFactory::convertToFlat(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    std::vector<Vertex> newVertices;
    std::vector<uint32_t> newIndices;

    newVertices.reserve(indices.size());
    newIndices.reserve(indices.size());

    for (size_t i = 0; i < indices.size(); i += 3)
    {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i+1];
        uint32_t i2 = indices[i+2];

        Vertex v0 = vertices[i0];
        Vertex v1 = vertices[i1];
        Vertex v2 = vertices[i2];

        // 重新计算面法线
        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;
        glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));

        v0.normal = faceNormal;
        v1.normal = faceNormal;
        v2.normal = faceNormal;

        newVertices.push_back(v0);
        newVertices.push_back(v1);
        newVertices.push_back(v2);

        uint32_t startIdx = static_cast<uint32_t>(newVertices.size()) - 3;
        newIndices.push_back(startIdx);
        newIndices.push_back(startIdx + 1);
        newIndices.push_back(startIdx + 2);
    }

    vertices = std::move(newVertices);
    indices = std::move(newIndices);
}

std::shared_ptr<Model> GeometryFactory::createFrustum(float topRadius, float bottomRadius, float height, int slices, bool useFlatShade)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float halfH = height / 2.0f;

    // ==========================================
    // 1. 生成侧面 (Side)
    // ==========================================
    // 我们需要多生成一个点来闭合纹理坐标 (0.0 -> 1.0)
    for (int i = 0; i <= slices; ++i)
    {
        float u = (float)i / (float)slices;
        float theta = u * 2.0f * glm::pi<float>();

        float cosTheta = cos(theta);
        float sinTheta = sin(theta);

        // 顶点位置
        glm::vec3 topPos(cosTheta * topRadius, halfH, sinTheta * topRadius);
        glm::vec3 bottomPos(cosTheta * bottomRadius, -halfH, sinTheta * bottomRadius);

        // 1. 计算半径差 (底 - 顶)
        //    如果底比顶大 (圆锥)，diff > 0，法线应该朝上 (Y > 0)
        //    如果底比顶小 (倒圆台)，diff < 0，法线应该朝下 (Y < 0)
        float rDiff = bottomRadius - topRadius;

        // 2. 计算斜边长度 (勾股定理)
        //    用于归一化，确保法线长度为 1
        float slantLen = std::sqrt(rDiff * rDiff + height * height);

        // 3. 计算法线分量
        //    水平分量由高度决定 (面越陡，法线越平)
        //    垂直分量由半径差决定 (面越平，法线越竖)
        float nx = cosTheta * (height / slantLen);
        float ny = rDiff / slantLen; 
        float nz = sinTheta * (height / slantLen);

        glm::vec3 normal(nx, ny, nz);

        // 如果是棱柱/棱台（sides较少），通常需要 Flat Shading（每个面独立顶点），
        // 但为了代码简洁，这里使用 Smooth Shading（共用顶点）。
        // 如果觉得棱柱看起来太圆滑，可以后续改为每个面独立生成顶点。

        vertices.push_back(Vertex(bottomPos, normal, glm::vec2(u, 0.0f))); // 偶数索引
        vertices.push_back(Vertex(topPos, normal, glm::vec2(u, 1.0f)));    // 奇数索引
    }

    // 侧面索引生成 (Triangle Strip 逻辑转为 Triangles)
    for (int i = 0; i < slices; ++i)
    {
        // 当前列的两个顶点索引
        int currentBottom = i * 2;
        int currentTop = currentBottom + 1;
        // 下一列的两个顶点索引
        int nextBottom = currentBottom + 2;
        int nextTop = currentTop + 2;

        // 三角形 1
        indices.push_back(currentBottom);
        indices.push_back(currentTop);
        indices.push_back(nextBottom);

        // 三角形 2
        indices.push_back(currentTop);
        indices.push_back(nextTop);
        indices.push_back(nextBottom);
    }
    

    // ==========================================
    // 2. 生成顶盖 (Top Cap) - 如果半径 > 0
    // ==========================================
    if (topRadius > 1e-6)
    {
        uint32_t centerIndex = static_cast<uint32_t>(vertices.size());

        // 中心点
        vertices.push_back(Vertex(glm::vec3(0, halfH, 0), glm::vec3(0, 1, 0), glm::vec2(0.5f, 0.5f)));

        // 圆环点
        for (int i = 0; i <= slices; ++i)
        {
            float u = (float)i / (float)slices;
            float theta = u * 2.0f * glm::pi<float>();
            float x = cos(theta) * topRadius;
            float z = sin(theta) * topRadius;

            // 纹理坐标简单映射
            float uMap = (cos(theta) * 0.5f) + 0.5f;
            float vMap = (sin(theta) * 0.5f) + 0.5f;

            vertices.push_back(Vertex(glm::vec3(x, halfH, z), glm::vec3(0, 1, 0), glm::vec2(uMap, vMap)));
        }

        // 索引 (Triangle Fan)
        for (int i = 0; i < slices; ++i)
        {
            indices.push_back(centerIndex);
            indices.push_back(centerIndex + 1 + i + 1); // 下一点
            indices.push_back(centerIndex + 1 + i);     // 当前点
        }
    }

    // ==========================================
    // 3. 生成底盖 (Bottom Cap) - 如果半径 > 0
    // ==========================================
    if (bottomRadius > 1e-6)
    {
        uint32_t centerIndex = static_cast<uint32_t>(vertices.size());

        // 中心点
        vertices.push_back(Vertex(glm::vec3(0, -halfH, 0), glm::vec3(0, -1, 0), glm::vec2(0.5f, 0.5f)));

        // 圆环点
        for (int i = 0; i <= slices; ++i)
        {
            float u = (float)i / (float)slices;
            float theta = u * 2.0f * glm::pi<float>();
            float x = cos(theta) * bottomRadius;
            float z = sin(theta) * bottomRadius;

            float uMap = (cos(theta) * 0.5f) + 0.5f;
            float vMap = (sin(theta) * 0.5f) + 0.5f;

            vertices.push_back(Vertex(glm::vec3(x, -halfH, z), glm::vec3(0, -1, 0), glm::vec2(uMap, vMap)));
        }

        // 索引 (Triangle Fan) - 注意顺序相反以保持逆时针
        for (int i = 0; i < slices; ++i)
        {
            indices.push_back(centerIndex);
            indices.push_back(centerIndex + 1 + i);     // 当前点
            indices.push_back(centerIndex + 1 + i + 1); // 下一点
        }
    }

    if (useFlatShade) {
        convertToFlat(vertices, indices);
    }

    computeTangents(vertices, indices);

    return std::make_unique<Model>(vertices, indices);
}

std::shared_ptr<Model> GeometryFactory::createCube(float size)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    float h = size / 2.0f;

    // 前面 (Normal +Z)
    addQuad(vertices, indices,
            Vertex{glm::vec3(-h, -h, h), glm::vec3(0, 0, 1), glm::vec2(0, 0)},
            Vertex{glm::vec3(h, -h, h), glm::vec3(0, 0, 1), glm::vec2(1, 0)},
            Vertex{glm::vec3(h, h, h), glm::vec3(0, 0, 1), glm::vec2(1, 1)},
            Vertex{glm::vec3(-h, h, h), glm::vec3(0, 0, 1), glm::vec2(0, 1)});
    // 后面 (Normal -Z)
    addQuad(vertices, indices,
            Vertex{glm::vec3(h, -h, -h), glm::vec3(0, 0, -1), glm::vec2(0, 0)},
            Vertex{glm::vec3(-h, -h, -h), glm::vec3(0, 0, -1), glm::vec2(1, 0)},
            Vertex{glm::vec3(-h, h, -h), glm::vec3(0, 0, -1), glm::vec2(1, 1)},
            Vertex{glm::vec3(h, h, -h), glm::vec3(0, 0, -1), glm::vec2(0, 1)});
    // 左面 (Normal -X)
    addQuad(vertices, indices,
            Vertex{glm::vec3(-h, -h, -h), glm::vec3(-1, 0, 0), glm::vec2(0, 0)},
            Vertex{glm::vec3(-h, -h, h), glm::vec3(-1, 0, 0), glm::vec2(1, 0)},
            Vertex{glm::vec3(-h, h, h), glm::vec3(-1, 0, 0), glm::vec2(1, 1)},
            Vertex{glm::vec3(-h, h, -h), glm::vec3(-1, 0, 0), glm::vec2(0, 1)});
    // 右面 (Normal +X)
    addQuad(vertices, indices,
            Vertex{glm::vec3(h, -h, h), glm::vec3(1, 0, 0), glm::vec2(0, 0)},
            Vertex{glm::vec3(h, -h, -h), glm::vec3(1, 0, 0), glm::vec2(1, 0)},
            Vertex{glm::vec3(h, h, -h), glm::vec3(1, 0, 0), glm::vec2(1, 1)},
            Vertex{glm::vec3(h, h, h), glm::vec3(1, 0, 0), glm::vec2(0, 1)});
    // 上面 (Normal +Y)
    addQuad(vertices, indices,
            Vertex{glm::vec3(-h, h, h), glm::vec3(0, 1, 0), glm::vec2(0, 0)},
            Vertex{glm::vec3(h, h, h), glm::vec3(0, 1, 0), glm::vec2(1, 0)},
            Vertex{glm::vec3(h, h, -h), glm::vec3(0, 1, 0), glm::vec2(1, 1)},
            Vertex{glm::vec3(-h, h, -h), glm::vec3(0, 1, 0), glm::vec2(0, 1)});
    // 下面 (Normal -Y)
    addQuad(vertices, indices,
            Vertex{glm::vec3(-h, -h, -h), glm::vec3(0, -1, 0), glm::vec2(0, 0)},
            Vertex{glm::vec3(h, -h, -h), glm::vec3(0, -1, 0), glm::vec2(1, 0)},
            Vertex{glm::vec3(h, -h, h), glm::vec3(0, -1, 0), glm::vec2(1, 1)},
            Vertex{glm::vec3(-h, -h, h), glm::vec3(0, -1, 0), glm::vec2(0, 1)});

    computeTangents(vertices, indices);

    return std::make_unique<Model>(vertices, indices);
}

std::shared_ptr<Model> GeometryFactory::createPlane(float width, float depth)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    float w = width / 2.0f;
    float d = depth / 2.0f;

    // 一个向上平铺的大矩形
    addQuad(vertices, indices,
            Vertex{glm::vec3(-w, 0, d), glm::vec3(0, 1, 0), glm::vec2(0, 0)},         // 左下
            Vertex{glm::vec3(w, 0, d), glm::vec3(0, 1, 0), glm::vec2(width, 0)},      // 右下 (UV重复)
            Vertex{glm::vec3(w, 0, -d), glm::vec3(0, 1, 0), glm::vec2(width, depth)}, // 右上
            Vertex{glm::vec3(-w, 0, -d), glm::vec3(0, 1, 0), glm::vec2(0, depth)}     // 左上
    );

    computeTangents(vertices, indices);

    return std::make_unique<Model>(vertices, indices);
}

std::shared_ptr<Model> GeometryFactory::createSphere(float radius, int stacks, int slices, bool useFlatShade)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (int i = 0; i <= stacks; ++i)
    {
        float v = (float)i / (float)stacks;
        float phi = v * glm::pi<float>();

        for (int j = 0; j <= slices; ++j)
        {
            float u = (float)j / (float)slices;
            float theta = u * 2.0f * glm::pi<float>();

            float x = cos(theta) * sin(phi);
            float y = cos(phi);
            float z = sin(theta) * sin(phi);

            glm::vec3 pos = glm::vec3(x, y, z) * radius;
            glm::vec3 normal = glm::vec3(x, y, z);
            glm::vec2 uv = glm::vec2(u, v);

            vertices.push_back(Vertex(pos, normal, uv));
        }
    }

    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            int first = (i * (slices + 1)) + j;
            int second = first + slices + 1;

            indices.push_back(first);
            indices.push_back(first + 1);
            indices.push_back(second);

            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second + 1);
        }
    }

    if (useFlatShade) {
        convertToFlat(vertices, indices);
    }

    computeTangents(vertices, indices);

    return std::make_unique<Model>(vertices, indices);
}

std::shared_ptr<Model> GeometryFactory::createCylinder(float radius, float height, int slices, bool useFlatShade)
{
    return createFrustum(radius, radius, height, slices, useFlatShade);
}

std::shared_ptr<Model> GeometryFactory::createCone(float radius, float height, int slices, bool useFlatShade)
{
    return createFrustum(0.0f, radius, height, slices, useFlatShade);
}

std::shared_ptr<Model> GeometryFactory::createPrism(float radius, float height, int sides, bool useFlatShade)
{
    // 棱柱本质上就是 slices 很少的圆柱
    return createFrustum(radius, radius, height, sides, useFlatShade);
}

std::shared_ptr<Model> GeometryFactory::createPyramidFrustum(float topRadius, float bottomRadius, float height, int sides, bool useFlatShade)
{
    // 棱台本质上就是 slices 很少的圆台
    return createFrustum(topRadius, bottomRadius, height, sides, useFlatShade);
}

void GeometryFactory::computeTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    // 1. 初始化所有切线为 0
    for (auto& v : vertices) {
        v.tangent = glm::vec4(0.0f);
    }

    // 2. 遍历所有三角形，累加切线
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        Vertex& v0 = vertices[indices[i]];
        Vertex& v1 = vertices[indices[i+1]];
        Vertex& v2 = vertices[indices[i+2]];

        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;

        glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
        glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

        glm::vec3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        // 累加到三个顶点上 (平滑切线)
        v0.tangent += glm::vec4(tangent, 0.0f);
        v1.tangent += glm::vec4(tangent, 0.0f);
        v2.tangent += glm::vec4(tangent, 0.0f);
    }

    // 3. 正交化并设置 w
    for (auto& v : vertices)
    {
        glm::vec3 t = glm::vec3(v.tangent);
        glm::vec3 n = v.normal;
        
        // Gram-Schmidt 正交化
        t = glm::normalize(t - n * glm::dot(n, t));
        
        // 对于工厂生成的标准几何体，没有镜像 UV，手性 w 设为 1.0
        v.tangent = glm::vec4(t, 1.0f);
    }
}