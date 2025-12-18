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

std::shared_ptr<Model> GeometryFactory::createFrustum(float topRadius, float bottomRadius, float height, int slices, bool useFlatShade)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float halfH = height / 2.0f;

    if (useFlatShade)
    {
        // === Flat Shading 逻辑 (修正版) ===
        for (int i = 0; i < slices; ++i)
        {
            float u0 = (float)i / (float)slices;
            float u1 = (float)(i + 1) / (float)slices;
            
            float theta0 = u0 * 2.0f * glm::pi<float>();
            float theta1 = u1 * 2.0f * glm::pi<float>();

            // 顶点位置计算
            // theta0 是 "当前角度" (右侧), theta1 是 "下一角度" (左侧)
            // 假设我们从外侧看，逆时针旋转
            glm::vec3 p0_top(cos(theta0) * topRadius, halfH, sin(theta0) * topRadius);    // 右上
            glm::vec3 p1_top(cos(theta1) * topRadius, halfH, sin(theta1) * topRadius);    // 左上
            glm::vec3 p0_btm(cos(theta0) * bottomRadius, -halfH, sin(theta0) * bottomRadius); // 右下
            glm::vec3 p1_btm(cos(theta1) * bottomRadius, -halfH, sin(theta1) * bottomRadius); // 左下

            // [修正 1] 法线计算
            // 我们需要法线朝外。
            // 向量 A: 从底到顶 (Up) = p0_top - p0_btm
            // 向量 B: 从右到左 (Left) = p1_btm - p0_btm
            // Cross(Up, Left) = Outward (朝外)
            glm::vec3 edgeUp = p0_top - p0_btm;
            glm::vec3 edgeLeft = p1_btm - p0_btm;
            glm::vec3 faceNormal = glm::normalize(glm::cross(edgeUp, edgeLeft));

            uint32_t baseIdx = (uint32_t)vertices.size();
            
            // 添加顶点 (顺序不重要，重要的是索引如何连接)
            vertices.push_back(Vertex(p0_btm, faceNormal, glm::vec2(u0, 0.0f))); // 0: 右下
            vertices.push_back(Vertex(p1_btm, faceNormal, glm::vec2(u1, 0.0f))); // 1: 左下
            vertices.push_back(Vertex(p1_top, faceNormal, glm::vec2(u1, 1.0f))); // 2: 左上
            vertices.push_back(Vertex(p0_top, faceNormal, glm::vec2(u0, 1.0f))); // 3: 右上

            // [修正 2] 索引绕序 (CCW)
            // 必须保证：点 -> 下一点 -> 再下一点 是逆时针的
            
            // 三角形 1: 右下 -> 右上 -> 左上 (0 -> 3 -> 2)
            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 3);
            indices.push_back(baseIdx + 2);

            // 三角形 2: 右下 -> 左上 -> 左下 (0 -> 2 -> 1)
            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 1);
        }
    }
    else
    {
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