#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <limits>
#include "base/vertex.h"
#include "base/bounding_box.h" // 你的 base 里应该有这个，如果没有请看 Model 类里的定义

struct Ray
{
    glm::vec3 origin;
    glm::vec3 direction;

    Ray(const glm::vec3 &o, const glm::vec3 &d) : origin(o), direction(d) {}
};

class PhysicsUtils
{
public:
    // 射线与 AABB (轴对齐包围盒) 的相交检测
    // 算法：Slab Method
    // 输入：射线 (ray)，包围盒 (box)
    // 输出：是否相交，如果相交，tMin 返回交点距离
    static bool intersectRayAABB(const Ray &ray, const BoundingBox &box, float &tMinResult)
    {
        // 防止射线方向为 0 导致的除零异常
        if (glm::length(ray.direction) < 1e-6f) return false;

        float tMin = 0.0f;
        float tMax = std::numeric_limits<float>::max();

        glm::vec3 boxMin = box.min;
        glm::vec3 boxMax = box.max;

        // 对 X, Y, Z 三个轴分别进行检测
        for (int i = 0; i < 3; i++)
        {
            float invD = 1.0f / ray.direction[i];
            float t0 = (boxMin[i] - ray.origin[i]) * invD;
            float t1 = (boxMax[i] - ray.origin[i]) * invD;

            if (invD < 0.0f)
                std::swap(t0, t1);

            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);

            if (tMax <= tMin)
                return false;
        }

        tMinResult = tMin;
        return true;
    }

    // Ray-Triangle (Möller–Trumbore) - 精测
    static bool intersectRayTriangle(const Ray& ray, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& outT)
    {
        const float EPSILON = 0.0000001f;
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 h = glm::cross(ray.direction, edge2);
        float a = glm::dot(edge1, h);

        // 如果 a 接近 0，说明射线平行于三角形
        if (a > -EPSILON && a < EPSILON)
            return false;

        float f = 1.0f / a;
        glm::vec3 s = ray.origin - v0;
        float u = f * glm::dot(s, h);

        if (u < 0.0f || u > 1.0f)
            return false;

        glm::vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(ray.direction, q);

        if (v < 0.0f || u + v > 1.0f)
            return false;

        // 计算 t
        float t = f * glm::dot(edge2, q);

        if (t > EPSILON) // 射线相交
        {
            outT = t;
            return true;
        }
        return false;
    }

    // ==========================================
    // 3. Ray-Mesh - 遍历所有三角形
    // ==========================================
    // 输入：局部空间的射线、顶点列表、索引列表
    // 输出：是否击中，tMin 返回最近的距离
    static bool intersectRayMesh(const Ray& localRay, 
                                 const std::vector<Vertex>& vertices, 
                                 const std::vector<uint32_t>& indices, 
                                 float& tMin)
    {
        bool hit = false;
        float closestT = std::numeric_limits<float>::max();

        // 遍历所有三角形 (每次步进 3)
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const glm::vec3& v0 = vertices[indices[i]].position;
            const glm::vec3& v1 = vertices[indices[i+1]].position;
            const glm::vec3& v2 = vertices[indices[i+2]].position;

            float t = 0.0f;
            if (intersectRayTriangle(localRay, v0, v1, v2, t))
            {
                if (t < closestT)
                {
                    closestT = t;
                    hit = true;
                }
            }
        }

        if (hit)
        {
            tMin = closestT;
            return true;
        }
        return false;
    }
};