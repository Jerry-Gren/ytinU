#include "obj_loader.h"
#include "geometry_factory.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

// 辅助函数：安全地将 OBJ 索引转换为 vector 下标
// 1. 处理正数索引 (1-based -> 0-based)
// 2. 处理负数索引 (相对位置 -> 绝对位置)
// 3. 边界检查
static int fixIndex(int idx, size_t size) {
    if (idx > 0) return idx - 1;             // 正数：1 -> 0
    if (idx < 0) return (int)size + idx;     // 负数：-1 -> size-1
    return -1;                               // 0 是无效索引
}

// 辅助函数：将 "1/2/3" 这种字符串分割成索引
// 返回 {posIndex, texIndex, normIndex}，如果某项缺失返回 -1
static glm::ivec3 parseFaceIndex(const std::string& token, size_t vSize, size_t vtSize, size_t vnSize) {
    glm::ivec3 result(-1);
    std::string part;
    std::stringstream ss(token);
    
    // 1. Position Index
    if (std::getline(ss, part, '/')) {
        if (!part.empty()) {
            try {
                int rawIdx = std::stoi(part);
                result.x = fixIndex(rawIdx, vSize);
            } catch (...) { result.x = -1; }
        }
    }
    
    // 2. TexCoord Index
    if (std::getline(ss, part, '/')) {
        if (!part.empty()) {
            try {
                int rawIdx = std::stoi(part);
                result.y = fixIndex(rawIdx, vtSize);
            } catch (...) { result.y = -1; }
        }
    }

    // 3. Normal Index
    if (std::getline(ss, part, '/')) {
        if (!part.empty()) {
            try {
                int rawIdx = std::stoi(part);
                result.z = fixIndex(rawIdx, vnSize);
            } catch (...) { result.z = -1; }
        }
    }

    return result;
}

MeshData OBJLoader::load(const std::string& filepath, bool useFlatShade) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // 抛出异常供 Model 捕获，或者打印错误并返回空数据
        throw std::runtime_error("[OBJ Loader] Failed to open OBJ file: " + filepath);
    }

    // 临时存储原始数据
    std::vector<glm::vec3> temp_positions;
    std::vector<glm::vec3> temp_normals;
    std::vector<glm::vec2> temp_texCoords;

    MeshData meshData; // 最终返回的数据

    // 顶点去重 Map
    std::unordered_map<Vertex, uint32_t> uniqueVertices;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string type;
        ss >> type;

        if (type == "v") {
            glm::vec3 v;
            ss >> v.x >> v.y >> v.z;
            temp_positions.push_back(v);
        }
        else if (type == "vn") {
            glm::vec3 vn;
            ss >> vn.x >> vn.y >> vn.z;
            temp_normals.push_back(vn);
        }
        else if (type == "vt") {
            glm::vec2 vt;
            ss >> vt.x >> vt.y;
            meshData.hasUVs = true;
            temp_texCoords.push_back(vt);
        }
        else if (type == "f") {
            std::string token;
            std::vector<Vertex> faceVertices;

            // 读取面数据
            while (ss >> token) {
                glm::ivec3 indices = parseFaceIndex(token, temp_positions.size(), temp_texCoords.size(), temp_normals.size());
                Vertex currentVertex;
                
                // Position
                if (indices.x >= 0 && indices.x < temp_positions.size())
                    currentVertex.position = temp_positions[indices.x];
                else
                    continue;

                // TexCoord
                if (indices.y >= 0 && indices.y < temp_texCoords.size())
                    currentVertex.texCoord = temp_texCoords[indices.y];
                else
                    currentVertex.texCoord = glm::vec2(0.0f);

                // Normal
                if (indices.z >= 0 && indices.z < temp_normals.size())
                    currentVertex.normal = temp_normals[indices.z];
                else
                    currentVertex.normal = glm::vec3(0.0f);

                faceVertices.push_back(currentVertex);
            }

            // 三角化 (Triangle Fan)
            // 将多边形分解为三角形
            if (faceVertices.size() >= 3) {
                for (size_t i = 1; i < faceVertices.size() - 1; ++i) {
                    Vertex triVerts[3] = {faceVertices[0], faceVertices[i], faceVertices[i+1]};

                    if (useFlatShade)
                    {
                        // 1. 强制计算面法线 (忽略 OBJ 文件自带的法线)
                        glm::vec3 edge1 = triVerts[1].position - triVerts[0].position;
                        glm::vec3 edge2 = triVerts[2].position - triVerts[0].position;
                        glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));

                        // 2. 将三个顶点直接加入，不做去重
                        for (int k = 0; k < 3; ++k) {
                            triVerts[k].normal = faceNormal; // 覆盖法线
                            
                            // 直接 push，不查 map
                            meshData.indices.push_back(static_cast<uint32_t>(meshData.vertices.size()));
                            meshData.vertices.push_back(triVerts[k]);
                        }
                    }
                    else
                    {
                        for (int k = 0; k < 3; ++k) {
                            if (uniqueVertices.count(triVerts[k]) == 0) {
                                uniqueVertices[triVerts[k]] = static_cast<uint32_t>(meshData.vertices.size());
                                meshData.vertices.push_back(triVerts[k]);
                            }
                            meshData.indices.push_back(uniqueVertices[triVerts[k]]);
                        }
                    }
                }
            }
        }
    }

    // 自动计算法线（如果 OBJ 文件里完全没有法线信息）
    if (!useFlatShade && temp_normals.empty()) {
        for (size_t i = 0; i < meshData.indices.size(); i += 3) {
            Vertex& v0 = meshData.vertices[meshData.indices[i]];
            Vertex& v1 = meshData.vertices[meshData.indices[i+1]];
            Vertex& v2 = meshData.vertices[meshData.indices[i+2]];

            glm::vec3 edge1 = v1.position - v0.position;
            glm::vec3 edge2 = v2.position - v0.position;
            glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

            v0.normal = normal;
            v1.normal = normal;
            v2.normal = normal;
        }
    }

    // 既然我们已经有了 meshData (vertices/indices)，直接借用 GeometryFactory 的算法
    // 需要 include "geometry_factory.h"
    GeometryFactory::computeTangents(meshData.vertices, meshData.indices);

    std::cout << "Loaded OBJ: " << filepath << "\n" 
              << "  Vertices: " << meshData.vertices.size() << "\n" 
              << "  Indices: " << meshData.indices.size() << std::endl;

    return meshData;
}

std::vector<SubMesh> OBJLoader::loadScene(const std::string& filepath, bool useFlatShade) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("[OBJ Loader] Failed to open OBJ file for scene: " + filepath);
    }

    std::vector<SubMesh> meshes;
    
    // 全局数据池 (v, vn, vt 是跨物体共享索引的)
    std::vector<glm::vec3> global_positions;
    std::vector<glm::vec3> global_normals;
    std::vector<glm::vec2> global_texCoords;

    // 当前正在构建的 SubMesh
    SubMesh currentMesh;
    currentMesh.name = "Object_0";
    currentMesh.hasUVs = false;

    // 当前 SubMesh 的顶点去重 Map
    std::unordered_map<Vertex, uint32_t> uniqueVertices;

    // 辅助 lambda：用于在切换物体或结束时保存当前 Mesh
    auto flushCurrentMesh = [&]() {
        if (!currentMesh.indices.empty()) {
            
            // 如果需要自动计算法线 (Smooth 且源文件没法线)
            if (!useFlatShade && global_normals.empty()) {
                 for (size_t i = 0; i < currentMesh.indices.size(); i += 3) {
                    Vertex& v0 = currentMesh.vertices[currentMesh.indices[i]];
                    Vertex& v1 = currentMesh.vertices[currentMesh.indices[i+1]];
                    Vertex& v2 = currentMesh.vertices[currentMesh.indices[i+2]];
                    glm::vec3 edge1 = v1.position - v0.position;
                    glm::vec3 edge2 = v2.position - v0.position;
                    glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
                    v0.normal = normal; v1.normal = normal; v2.normal = normal;
                }
            }
            meshes.push_back(currentMesh);
        }
        
        // 重置状态
        currentMesh = SubMesh();
        uniqueVertices.clear();
        currentMesh.hasUVs = false; 
    };

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string type;
        ss >> type;

        // --- 读取全局数据 ---
        if (type == "v") {
            glm::vec3 v; ss >> v.x >> v.y >> v.z;
            global_positions.push_back(v);
        }
        else if (type == "vn") {
            glm::vec3 vn; ss >> vn.x >> vn.y >> vn.z;
            global_normals.push_back(vn);
        }
        else if (type == "vt") {
            glm::vec2 vt; ss >> vt.x >> vt.y;
            global_texCoords.push_back(vt);
        }

        // --- 切换物体 (o) 或 组 (g) ---
        else if (type == "o" || type == "g") {
            // 保存上一个物体
            flushCurrentMesh();

            // 读取新名字 (读取整行，因为名字可能包含空格)
            std::string name;
            std::getline(ss, name); 
            
            // std::getline 会读取前面的空格，我们需要 Trim (修剪) 一下
            size_t first = name.find_first_not_of(" \t\r");
            size_t last = name.find_last_not_of(" \t\r");
            
            if (first != std::string::npos && last != std::string::npos) {
                currentMesh.name = name.substr(first, last - first + 1);
            } else {
                // 如果全是空格或者为空
                currentMesh.name = "Object"; 
            }
        }

        // --- 读取面 (f) ---
        else if (type == "f") {
            std::string token;
            std::vector<Vertex> faceVertices;

            while (ss >> token) {
                // 注意：这里传入的是当前全局池的大小
                glm::ivec3 indices = parseFaceIndex(token, global_positions.size(), global_texCoords.size(), global_normals.size());
                Vertex currentVertex;

                // 1. Position
                if (indices.x >= 0 && indices.x < global_positions.size())
                    currentVertex.position = global_positions[indices.x];
                else continue;

                // 2. TexCoord
                if (indices.y >= 0 && indices.y < global_texCoords.size()) {
                    currentVertex.texCoord = global_texCoords[indices.y];
                    currentMesh.hasUVs = true; // 只要用到了 UV 索引，就标记有 UV
                } else {
                    currentVertex.texCoord = glm::vec2(0.0f);
                }

                // 3. Normal
                if (indices.z >= 0 && indices.z < global_normals.size())
                    currentVertex.normal = global_normals[indices.z];
                else 
                    currentVertex.normal = glm::vec3(0.0f);

                faceVertices.push_back(currentVertex);
            }

            // 三角化
            if (faceVertices.size() >= 3) {
                for (size_t i = 1; i < faceVertices.size() - 1; ++i) {
                    Vertex triVerts[3] = {faceVertices[0], faceVertices[i], faceVertices[i+1]};

                    if (useFlatShade) {
                        // Flat Shading 逻辑：分裂顶点，重算法线
                        glm::vec3 edge1 = triVerts[1].position - triVerts[0].position;
                        glm::vec3 edge2 = triVerts[2].position - triVerts[0].position;
                        glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));

                        for (int k = 0; k < 3; ++k) {
                            triVerts[k].normal = faceNormal;
                            currentMesh.indices.push_back(static_cast<uint32_t>(currentMesh.vertices.size()));
                            currentMesh.vertices.push_back(triVerts[k]);
                        }
                    } 
                    else {
                        // Smooth Shading 逻辑：使用 Map 去重
                        for (int k = 0; k < 3; ++k) {
                            if (uniqueVertices.count(triVerts[k]) == 0) {
                                uniqueVertices[triVerts[k]] = static_cast<uint32_t>(currentMesh.vertices.size());
                                currentMesh.vertices.push_back(triVerts[k]);
                            }
                            currentMesh.indices.push_back(uniqueVertices[triVerts[k]]);
                        }
                    }
                }
            }
        }
    }

    // 循环结束，保存最后一个物体
    flushCurrentMesh();

    std::cout << "Loaded Scene OBJ: " << filepath << " containing " << meshes.size() << " meshes." << std::endl;
    return meshes;
}