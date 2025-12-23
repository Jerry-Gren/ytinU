#pragma once

#include <string>
#include <vector>
#include "asset_data.h"
#include "base/vertex.h"

class OBJLoader {
public:
    // 修改返回类型为 MeshData
    // 如果加载失败，选择抛出异常，或者返回空的 MeshData（根据你的错误处理策略）
    static MeshData load(const std::string& filepath, bool useFlatShade = false, const std::string& targetSubMeshName = "");

    // 场景加载 (返回多个子网格)
    static std::vector<SubMesh> loadScene(const std::string& filepath, bool useSplitVert = false);
};