#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <limits>
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
};