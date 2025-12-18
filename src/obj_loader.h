#pragma once

#include <string>
#include <vector>
#include "base/vertex.h"

// 定义一个中间结构体，用于在 Loader 和 Model 之间传递数据
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

class OBJLoader {
public:
    // 修改返回类型为 MeshData
    // 如果加载失败，选择抛出异常，或者返回空的 MeshData（根据你的错误处理策略）
    static MeshData load(const std::string& filepath, bool useFlatShade = false);
};