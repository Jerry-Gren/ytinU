#include "obj_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

// 辅助函数：将 "1/2/3" 这种字符串分割成索引
// 返回 {posIndex, texIndex, normIndex}，如果某项缺失返回 -1
// OBJ 索引从 1 开始，所以我们需要在解析时减 1
static glm::ivec3 parseFaceIndex(const std::string& token) {
    glm::ivec3 result(-1);
    std::string part;
    std::stringstream ss(token);
    
    // 1. Position Index
    if (std::getline(ss, part, '/')) {
        if (!part.empty()) result.x = std::stoi(part) - 1;
    }
    
    // 2. TexCoord Index
    if (std::getline(ss, part, '/')) {
        if (!part.empty()) result.y = std::stoi(part) - 1;
    }

    // 3. Normal Index
    if (std::getline(ss, part, '/')) {
        if (!part.empty()) result.z = std::stoi(part) - 1;
    }

    return result;
}

MeshData OBJLoader::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // 抛出异常供 Model 捕获，或者打印错误并返回空数据
        throw std::runtime_error("Failed to open OBJ file: " + filepath);
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
        if (line.empty()) continue;

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
            temp_texCoords.push_back(vt);
        }
        else if (type == "f") {
            std::string token;
            std::vector<Vertex> faceVertices;

            // 读取面数据
            while (ss >> token) {
                glm::ivec3 indices = parseFaceIndex(token);
                Vertex currentVertex;
                
                // Position
                if (indices.x >= 0 && indices.x < temp_positions.size())
                    currentVertex.position = temp_positions[indices.x];

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

    // 自动计算法线（如果 OBJ 文件里完全没有法线信息）
    if (temp_normals.empty()) {
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

    std::cout << "Loaded OBJ: " << filepath << "\n" 
              << "  Vertices: " << meshData.vertices.size() << "\n" 
              << "  Indices: " << meshData.indices.size() << std::endl;

    return meshData;
}