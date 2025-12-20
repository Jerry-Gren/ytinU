#pragma once

#include <string>
#include <vector>
#include "base/vertex.h"

// 定义一个中间结构体，用于在 Loader 和 Model 之间传递数据
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    bool hasUVs = false;
};

struct SubMesh {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    bool hasUVs = false;
};

class OBJLoader {
public:
    // 修改返回类型为 MeshData
    // 如果加载失败，选择抛出异常，或者返回空的 MeshData（根据你的错误处理策略）
    static MeshData load(const std::string& filepath, bool useFlatShade = false);

    // 场景加载 (返回多个子网格)
    static std::vector<SubMesh> loadScene(const std::string& filepath, bool useSplitVert = false);
};