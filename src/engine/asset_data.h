#pragma once

#include <vector>
#include <string>
#include "base/vertex.h" // 确保 Vertex 结构体可见

// ==========================================
// 1. 纹理相关数据
// ==========================================

// 原始 HDR 图像数据 (stbi_loadf 的结果)
struct HDRData {
    float* data = nullptr;
    int width = 0;
    int height = 0;
    int components = 0;
    bool isValid() const { return data != nullptr; }
};

// ==========================================
// 2. 模型相关数据
// ==========================================

// 单个网格的原始数据 (Loader -> ResourceManager)
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    bool hasUVs = false;
};

// 场景中的子网格定义 (含名称)
struct SubMesh {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    bool hasUVs = false;
};