#pragma once

#include "bounding_box.h"
#include "plane.h"
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

struct Frustum {
public:
    Plane planes[6];
    enum {
        LeftFace = 0,
        RightFace = 1,
        BottomFace = 2,
        TopFace = 3,
        NearFace = 4,
        FarFace = 5
    };

    static Frustum createFromMatrix(const glm::mat4& vp) {
        Frustum out;
        
        // 矩阵的行 (或列，取决于 GLM 的内存布局，GLM 是列主序，但访问时 m[col][row])
        // Gribb-Hartmann 方法提取平面:
        // Left:   row4 + row1
        // Right:  row4 - row1
        // Bottom: row4 + row2
        // Top:    row4 - row2
        // Near:   row4 + row3
        // Far:    row4 - row3
        
        // 为了方便，我们转置一下或者直接用行访问
        const float* m = (const float*)glm::value_ptr(vp);
        
        // 辅助 lambda: 从系数构建标准化平面
        // Ax + By + Cz + D = 0
        auto buildPlane = [&](int index, float a, float b, float c, float d) {
            out.planes[index].normal = glm::vec3(a, b, c);
            out.planes[index].signedDistance = d; // 注意 plane.h 的定义，通常 d 是常数项
            out.planes[index].normalize(); // 必须归一化才能正确计算距离
        };

        // GLM 是列主序，m[0]~m[3]是第一列。
        // 公式通常基于行向量: P = A * V
        // Row 1: m[0], m[4], m[8],  m[12]
        // Row 2: m[1], m[5], m[9],  m[13]
        // Row 3: m[2], m[6], m[10], m[14]
        // Row 4: m[3], m[7], m[11], m[15]

        // Left: w + x
        buildPlane(LeftFace, 
            m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]);

        // Right: w - x
        buildPlane(RightFace, 
            m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]);

        // Bottom: w + y
        buildPlane(BottomFace, 
            m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]);

        // Top: w - y
        buildPlane(TopFace, 
            m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]);

        // Near: w + z
        buildPlane(NearFace, 
            m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]);

        // Far: w - z
        buildPlane(FarFace, 
            m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);

        return out;
    }

    bool intersect(const BoundingBox& aabb, const glm::mat4& modelMatrix) const {
        // Judge whether the frustum intersects the bounding box
        // Note: this is for Bonus project 'Frustum Culling'
        // https://learnopengl.com/Guest-Articles/2021/Scene/Frustum-Culling
        const glm::vec3 localCenter = (aabb.max + aabb.min) * 0.5f;
        const glm::vec3 localExtents = (aabb.max - aabb.min) * 0.5f;

        const glm::vec3 globalCenter = glm::vec3(modelMatrix * glm::vec4(localCenter, 1.0f));

        const glm::vec3 right = glm::vec3(modelMatrix[0]) * localExtents.x;
        const glm::vec3 up = glm::vec3(modelMatrix[1]) * localExtents.y;
        const glm::vec3 forward = glm::vec3(modelMatrix[2]) * localExtents.z;

        for (int i = 0; i < 6; ++i) {
            const Plane& plane = planes[i];

            // projection_radius = |n·right| + |n·up| + |n·forward|
            float r = glm::abs(glm::dot(plane.normal, right)) +
                    glm::abs(glm::dot(plane.normal, up)) +
                    glm::abs(glm::dot(plane.normal, forward));

            // signed_distance = n·center + d
            float dist = glm::dot(plane.normal, globalCenter) + plane.signedDistance;

            if (dist < -r) {
                return false;
            }
        }
        return true;
    }
};

inline std::ostream& operator<<(std::ostream& os, const Frustum& frustum) {
    os << "frustum: \n";
    os << "planes[Left]:   " << frustum.planes[0] << "\n";
    os << "planes[Right]:  " << frustum.planes[1] << "\n";
    os << "planes[Bottom]: " << frustum.planes[2] << "\n";
    os << "planes[Top]:    " << frustum.planes[3] << "\n";
    os << "planes[Near]:   " << frustum.planes[4] << "\n";
    os << "planes[Far]:    " << frustum.planes[5] << "\n";

    return os;
}