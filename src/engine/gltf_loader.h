#pragma once

#include <string>
#include <vector>
#include "asset_data.h" // 定义了 SubMesh

class GLTFLoader {
public:
    static std::vector<SubMesh> loadScene(const std::string& filepath);
};