#pragma once

#include "bounding_box.h"
#include "plane.h"
#include <iostream>

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