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

std::shared_ptr<Model> GeometryFactory::createFrustum(float topRadius, float bottomRadius, float height, int slices)
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

        // 法线计算 (侧面法线)
        // 简单起见，我们假设侧面法线是水平的 (对于圆柱是完美的，对于圆锥略有误差但视觉可接受)
        // 如果要严谨的圆台法线，需要计算斜率，这里暂取水平方向
        glm::vec3 normal = glm::vec3(cosTheta, 0.0f, sinTheta);

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

    return std::make_unique<Model>(vertices, indices);
}

std::shared_ptr<Model> GeometryFactory::createSphere(float radius, int stacks, int slices)
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

    return std::make_unique<Model>(vertices, indices);
}

std::shared_ptr<Model> GeometryFactory::createCylinder(float radius, float height, int slices)
{
    return createFrustum(radius, radius, height, slices);
}

std::shared_ptr<Model> GeometryFactory::createCone(float radius, float height, int slices)
{
    return createFrustum(0.0f, radius, height, slices);
}

std::shared_ptr<Model> GeometryFactory::createPrism(float radius, float height, int sides)
{
    // 棱柱本质上就是 slices 很少的圆柱
    return createFrustum(radius, radius, height, sides);
}

std::shared_ptr<Model> GeometryFactory::createPyramidFrustum(float topRadius, float bottomRadius, float height, int sides)
{
    // 棱台本质上就是 slices 很少的圆台
    return createFrustum(topRadius, bottomRadius, height, sides);
}