#include "obj_loader.h"
#include "geometry_factory.h"
#include "utils/profiler.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <cstring> // for memcpy, memset
#include <cmath>   // for pow

// =================================================================================================
// Fast Parser Helpers (静态辅助函数，仅在本文件可见)
// =================================================================================================

// 1. 快速跳过空白字符 (空格, Tab, \r)
// 注意：不跳过换行符 \n，因为 OBJ 是基于行的
static inline void skipWhitespace(const char*& cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r') {
        cursor++;
    }
}

// 2. 快速跳到行尾 (用于注释或跳过未知行)
static inline void skipLine(const char*& cursor, const char* end) {
    while (cursor < end && *cursor != '\n') {
        cursor++;
    }
    if (cursor < end) cursor++; // 跳过 \n
}

// 3. 快速解析整数 (替代 std::stoi)
static inline int parseInt(const char*& cursor) {
    skipWhitespace(cursor);
    int sign = 1;
    if (*cursor == '-') {
        sign = -1;
        cursor++;
    } else if (*cursor == '+') {
        cursor++;
    }

    int value = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10 + (*cursor - '0');
        cursor++;
    }
    return value * sign;
}

// 4. 快速解析浮点数 (替代 std::stof)
// OBJ 里的浮点数通常很简单，不需要处理科学计数法(e-05)的所有边缘情况
// 这个实现比 std::stof 快 5-10 倍
static inline float parseFloat(const char*& cursor) {
    skipWhitespace(cursor);
    float sign = 1.0f;
    if (*cursor == '-') {
        sign = -1.0f;
        cursor++;
    } else if (*cursor == '+') {
        cursor++;
    }

    float value = 0.0f;
    // 整数部分
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10.0f + (*cursor - '0');
        cursor++;
    }

    // 小数部分
    if (*cursor == '.') {
        cursor++;
        float factor = 0.1f;
        while (*cursor >= '0' && *cursor <= '9') {
            value += (*cursor - '0') * factor;
            factor *= 0.1f;
            cursor++;
        }
    }
    
    // (可选) 科学计数法支持: 1.2e-3
    // 大多数标准 OBJ 导出器不使用科学计数法，但为了健壮性可以加上
    if (*cursor == 'e' || *cursor == 'E') {
        cursor++;
        int exp = parseInt(cursor); // 复用整数解析
        value *= std::pow(10.0f, (float)exp);
    }

    return value * sign;
}

// 5. 快速解析面索引 "v/vt/vn"
// 返回 {v, vt, vn}，如果某项缺失返回 -1
// 修改了 cursor 的位置
static inline glm::ivec3 parseFaceIndex(const char*& cursor, size_t vSize, size_t vtSize, size_t vnSize) {
    glm::ivec3 result(-1);
    
    // 1. 解析 v
    result.x = parseInt(cursor);
    
    // 处理 v 索引转换 (1-based -> 0-based, negative -> relative)
    if (result.x > 0) result.x -= 1;
    else if (result.x < 0) result.x += (int)vSize;

    if (*cursor == '/') {
        cursor++;
        
        // 检查是否有 vt (例如 "1//3" 这种情况就是没有 vt)
        if (*cursor != '/') {
            result.y = parseInt(cursor);
            if (result.y > 0) result.y -= 1;
            else if (result.y < 0) result.y += (int)vtSize;
        }

        if (*cursor == '/') {
            cursor++;
            // 解析 vn
            result.z = parseInt(cursor);
            if (result.z > 0) result.z -= 1;
            else if (result.z < 0) result.z += (int)vnSize;
        }
    }

    return result;
}

// =================================================================================================
// OBJLoader 实现
// =================================================================================================

// load 单体函数保持旧逻辑或可以简单封装 loadScene，这里为了节省篇幅，聚焦 loadScene
MeshData OBJLoader::load(const std::string& filepath, bool useFlatShade, const std::string& targetSubMeshName) {
    // 简单复用 loadScene，只取第一个 Mesh 或匹配的 Mesh
    auto meshes = loadScene(filepath, useFlatShade);
    MeshData data;
    if (meshes.empty()) return data;

    if (targetSubMeshName.empty()) {
        // 如果没有指定名称，且只有一个 mesh，直接返回
        if (meshes.size() == 1) {
            data.vertices = std::move(meshes[0].vertices);
            data.indices = std::move(meshes[0].indices);
            data.hasUVs = meshes[0].hasUVs;
        } else {
            // 如果有多个 mesh，我们需要把它们合并成一个 MeshData
            // 或者现在的架构其实不需要合并，因为 ResourceManager::getModel 应该只用于简单的单体
            // 这里我们暂时只返回第一个，或者抛出警告
            // 为了兼容性，返回第一个非空的
             data.vertices = std::move(meshes[0].vertices);
             data.indices = std::move(meshes[0].indices);
             data.hasUVs = meshes[0].hasUVs;
        }
    } else {
        // 查找匹配的
        for (auto& m : meshes) {
            if (m.name == targetSubMeshName) {
                data.vertices = std::move(m.vertices);
                data.indices = std::move(m.indices);
                data.hasUVs = m.hasUVs;
                break;
            }
        }
    }
    return data;
}

std::vector<SubMesh> OBJLoader::loadScene(const std::string& filepath, bool useFlatShade) {
    ScopedTimer timer("OBJLoader::loadScene (" + filepath + ")");

    // 1. 一次性读取整个文件到 Buffer [Memory Mapped File 思想]
    std::ifstream file(filepath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("[OBJ Loader] Failed to open file: " + filepath);
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize + 1); // +1 为了末尾的安全哨兵
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    buffer[fileSize] = '\n'; // 确保最后有一个换行符，防止解析溢出

    // 2. 预分配内存
    // 经验估算：v 行通常占 1/3 到 1/2 的行数，假设每行平均 30 字节
    // 这是一个非常保守的预估，目的是减少 realloc
    size_t estimatedVerts = fileSize / 60; 
    
    std::vector<glm::vec3> global_positions;
    std::vector<glm::vec3> global_normals;
    std::vector<glm::vec2> global_texCoords;
    global_positions.reserve(estimatedVerts);
    global_normals.reserve(estimatedVerts);
    global_texCoords.reserve(estimatedVerts);

    std::vector<SubMesh> meshes;
    SubMesh currentMesh;
    currentMesh.name = "Default";
    currentMesh.hasUVs = false;
    currentMesh.vertices.reserve(estimatedVerts); // 预估
    currentMesh.indices.reserve(estimatedVerts);

    // 3. 指针解析循环
    const char* cursor = buffer.data();
    const char* end = buffer.data() + fileSize;

    // 当前解析状态
    std::string currentObjectName = "Object";
    std::string currentMaterialName = "Default";
    
    // 顶点去重 Map (Local per SubMesh)
    // 注意：std::unordered_map 可能在频繁插入时变慢，
    // 对于超大模型，直接插入 vertex 可能会比 map 查找更快 (trade memory for speed)
    // 但为了保持索引 buffer 小，我们还是用 map。
    std::unordered_map<Vertex, uint32_t> uniqueVertices;
    uniqueVertices.reserve(estimatedVerts); // 预留 Bucket

    auto flushCurrentMesh = [&]() {
        if (!currentMesh.indices.empty()) {
            // 自动计算法线
            if (!useFlatShade && global_normals.empty()) {
                // ... (法线计算逻辑同前) ...
                // 这里的计算量较大，如果模型自带法线则不会执行
                for (size_t i = 0; i < currentMesh.indices.size(); i += 3) {
                    Vertex& v0 = currentMesh.vertices[currentMesh.indices[i]];
                    Vertex& v1 = currentMesh.vertices[currentMesh.indices[i+1]];
                    Vertex& v2 = currentMesh.vertices[currentMesh.indices[i+2]];
                    glm::vec3 e1 = v1.position - v0.position;
                    glm::vec3 e2 = v2.position - v0.position;
                    glm::vec3 n = glm::normalize(glm::cross(e1, e2));
                    v0.normal = n; v1.normal = n; v2.normal = n;
                }
            }
            // 计算切线
            GeometryFactory::computeTangents(currentMesh.vertices, currentMesh.indices);
            
            meshes.push_back(std::move(currentMesh));
        }
        // Reset
        currentMesh = SubMesh();
        currentMesh.name = currentObjectName; // 继承当前对象名
        uniqueVertices.clear(); 
        // 重置后预留一点空间，防止下次立即 realloc
        uniqueVertices.reserve(1000); 
    };

    while (cursor < end) {
        // 跳过行首空白
        skipWhitespace(cursor);
        
        if (cursor >= end) break;

        char c = *cursor;

        // ---------------------------------------------------------
        // 顶点数据 (v, vt, vn)
        // ---------------------------------------------------------
        if (c == 'v') {
            cursor++; // skip 'v'
            if (*cursor == ' ') {
                // v: Position
                float x = parseFloat(cursor);
                float y = parseFloat(cursor);
                float z = parseFloat(cursor);
                global_positions.emplace_back(x, y, z);
            } 
            else if (*cursor == 't') {
                // vt: TexCoord
                cursor++;
                float u = parseFloat(cursor);
                float v = parseFloat(cursor);
                global_texCoords.emplace_back(u, v);
            } 
            else if (*cursor == 'n') {
                // vn: Normal
                cursor++;
                float x = parseFloat(cursor);
                float y = parseFloat(cursor);
                float z = parseFloat(cursor);
                global_normals.emplace_back(x, y, z);
            }
            skipLine(cursor, end);
        }
        // ---------------------------------------------------------
        // 面数据 (f)
        // ---------------------------------------------------------
        else if (c == 'f') {
            cursor++; // skip 'f'
            
            // 读取这一行的所有索引
            std::vector<glm::ivec3> faceIndices;
            // 预留 4 个，大多数面是三角形(3)或四边形(4)
            faceIndices.reserve(4); 

            while (cursor < end && *cursor != '\n') {
                skipWhitespace(cursor);
                if (*cursor >= '0' && *cursor <= '9' || *cursor == '-') {
                    glm::ivec3 idx = parseFaceIndex(cursor, global_positions.size(), global_texCoords.size(), global_normals.size());
                    faceIndices.push_back(idx);
                } else {
                    // 遇到未知字符，可能是行尾注释
                    break;
                }
            }
            skipLine(cursor, end); // 确保跳过换行符

            // 三角化 (Triangulation) - Triangle Fan
            if (faceIndices.size() >= 3) {
                for (size_t i = 1; i < faceIndices.size() - 1; ++i) {
                    glm::ivec3 faceVertsIdx[3] = { faceIndices[0], faceIndices[i], faceIndices[i+1] };

                    // 构建 3 个 Vertex
                    Vertex triVerts[3];
                    for(int k=0; k<3; ++k) {
                        glm::ivec3 idx = faceVertsIdx[k];
                        // Pos
                        if(idx.x != -1) triVerts[k].position = global_positions[idx.x];
                        // UV
                        if(idx.y != -1) {
                            triVerts[k].texCoord = global_texCoords[idx.y];
                            currentMesh.hasUVs = true;
                        }
                        // Normal
                        if(idx.z != -1) triVerts[k].normal = global_normals[idx.z];
                    }

                    // Flat Shading 处理
                    if (useFlatShade) {
                        glm::vec3 e1 = triVerts[1].position - triVerts[0].position;
                        glm::vec3 e2 = triVerts[2].position - triVerts[0].position;
                        glm::vec3 faceN = glm::normalize(glm::cross(e1, e2));
                        
                        for(int k=0; k<3; ++k) {
                            triVerts[k].normal = faceN;
                            currentMesh.indices.push_back((uint32_t)currentMesh.vertices.size());
                            currentMesh.vertices.push_back(triVerts[k]);
                        }
                    } 
                    else {
                        // Smooth Shading (Map 去重)
                        for(int k=0; k<3; ++k) {
                            auto it = uniqueVertices.find(triVerts[k]);
                            if (it != uniqueVertices.end()) {
                                currentMesh.indices.push_back(it->second);
                            } else {
                                uint32_t newIdx = (uint32_t)currentMesh.vertices.size();
                                uniqueVertices[triVerts[k]] = newIdx;
                                currentMesh.vertices.push_back(triVerts[k]);
                                currentMesh.indices.push_back(newIdx);
                            }
                        }
                    }
                }
            }
        }
        // ---------------------------------------------------------
        // 对象 / 组 (o, g)
        // ---------------------------------------------------------
        else if (c == 'o' || c == 'g') {
            // 如果是 'g'，要判断是不是紧跟在 'o' 后面，避免重复切分
            // 简单处理：只要遇到 o 或 g 且名字变了，就切分
            char type = c;
            cursor++;
            skipWhitespace(cursor);
            
            // 读取名字
            const char* nameStart = cursor;
            while (cursor < end && *cursor != '\n' && *cursor != '\r') cursor++;
            std::string name(nameStart, cursor - nameStart);
            
            // Trim
            size_t first = name.find_first_not_of(" \t");
            if (first != std::string::npos) {
                size_t last = name.find_last_not_of(" \t");
                name = name.substr(first, last - first + 1);
            } else {
                if (type == 'o') name = "Object";
                else name = "Group";
            }

            if (name != currentObjectName) {
                flushCurrentMesh();
                currentObjectName = name;
                currentMaterialName = "Default"; // 重置材质
                currentMesh.name = currentObjectName;
            }
            skipLine(cursor, end);
        }
        // ---------------------------------------------------------
        // 材质 (usemtl)
        // ---------------------------------------------------------
        else if (c == 'u') { // heuristic for "usemtl"
            if (strncmp(cursor, "usemtl", 6) == 0) {
                cursor += 6;
                skipWhitespace(cursor);
                
                const char* nameStart = cursor;
                while (cursor < end && *cursor != '\n' && *cursor != '\r') cursor++;
                std::string matName(nameStart, cursor - nameStart);
                
                // Trim
                size_t first = matName.find_first_not_of(" \t");
                if(first != std::string::npos) {
                    size_t last = matName.find_last_not_of(" \t");
                    matName = matName.substr(first, last - first + 1);
                }

                if (matName != currentMaterialName) {
                    flushCurrentMesh();
                    currentMaterialName = matName;
                    currentMesh.name = currentObjectName + "_" + currentMaterialName;
                }
            }
            skipLine(cursor, end);
        }
        // ---------------------------------------------------------
        // 其他 (注释 #, mtllib 等)
        // ---------------------------------------------------------
        else {
            skipLine(cursor, end);
        }
    }

    // 处理最后一个 Mesh
    flushCurrentMesh();

    std::cout << "Loaded Scene OBJ stats:" 
              << "\n  File Size: " << fileSize / 1024 << " KB"
              << "\n  Total SubMeshes: " << meshes.size()
              << "\n  Total Global Verts: " << global_positions.size() 
              << std::endl;

    return meshes;
}