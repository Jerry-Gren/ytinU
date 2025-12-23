#pragma once

#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 tangent;
    Vertex() = default;
    Vertex(const glm::vec3& p, const glm::vec3& n, const glm::vec2& texC, const glm::vec4& t = glm::vec4(0.0f))
        : position(p), normal(n), texCoord(texC), tangent(t) {}
    bool operator==(const Vertex& v) const {
        return (position == v.position) && (normal == v.normal) && (texCoord == v.texCoord) && (tangent == v.tangent);
    }
};

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

namespace std {
template <>
struct hash<Vertex> {
    size_t operator()(const Vertex& vertex) const {
        // [修改 3] hash 必须包含 tangent
        // 使用 glm::gtx::hash 的辅助函数组合哈希值
        size_t h1 = hash<glm::vec3>()(vertex.position);
        size_t h2 = hash<glm::vec3>()(vertex.normal);
        size_t h3 = hash<glm::vec2>()(vertex.texCoord);
        size_t h4 = hash<glm::vec4>()(vertex.tangent);

        // 一个简单的组合哈希算法 (类似 boost::hash_combine)
        size_t seed = 0;
        auto hash_combine = [&seed](size_t v) {
            seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };
        
        hash_combine(h1);
        hash_combine(h2);
        hash_combine(h3);
        hash_combine(h4);
        
        return seed;
    }
};
} // namespace std