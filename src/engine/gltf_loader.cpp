#include "gltf_loader.h"
#include "geometry_factory.h"
#include <iostream>
#include <filesystem>

// 定义宏以实现 tinygltf
// 注意：如果你项目中其他地方已经定义了 STB_IMAGE_IMPLEMENTATION，
// 可能需要在这里定义 TINYGLTF_NO_STB_IMAGE 并手动包含 stb_image.h
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION 
#define STB_IMAGE_WRITE_IMPLEMENTATION 
// #define TINYGLTF_NO_STB_IMAGE // 如果发生链接冲突，取消注释此行
// #define TINYGLTF_NO_STB_IMAGE_WRITE // 我们只读取，不需要写入功能

#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// 辅助函数：从 Accessor 和 BufferView 获取数据指针
// T 是我们期望的数据类型 (如 float, uint16_t 等)
template<typename T>
const T* getDataPointer(const tinygltf::Model& model, int accessorIndex) {
    if (accessorIndex < 0) return nullptr;
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

    // 计算起始地址：Buffer地址 + View偏移 + Accessor偏移
    const unsigned char* dataStart = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    return reinterpret_cast<const T*>(dataStart);
}

// 辅助函数：获取 Accessor 的步长 (Stride)
// 如果 BufferView 没有定义 stride，则根据 componentType 和 type 计算紧凑步长
int getStride(const tinygltf::Model& model, int accessorIndex) {
    if (accessorIndex < 0) return 0;
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    
    if (bufferView.byteStride > 0) {
        return bufferView.byteStride;
    }
    
    // 紧密排列 (Tightly packed)
    return tinygltf::GetComponentSizeInBytes(accessor.componentType) * tinygltf::GetNumComponentsInType(accessor.type);
}

// 处理单个 Primitive (对应我们的 SubMesh)
void processPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const std::string& name, std::vector<SubMesh>& outMeshes) {
    SubMesh subMesh;
    subMesh.name = name;
    subMesh.hasUVs = false;

    // -------------------------------------------------------------
    // 1. 获取 Attribute Accessor 索引
    // -------------------------------------------------------------
    int posIdx = -1;
    int normIdx = -1;
    int uv0Idx = -1;
    int tanIdx = -1;

    if (primitive.attributes.find("POSITION") != primitive.attributes.end())
        posIdx = primitive.attributes.at("POSITION");
    if (primitive.attributes.find("NORMAL") != primitive.attributes.end())
        normIdx = primitive.attributes.at("NORMAL");
    if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
        uv0Idx = primitive.attributes.at("TEXCOORD_0");
    if (primitive.attributes.find("TANGENT") != primitive.attributes.end())
        tanIdx = primitive.attributes.at("TANGENT");

    // 必须有位置数据
    if (posIdx < 0) return;

    // -------------------------------------------------------------
    // 2. 读取顶点数据
    // -------------------------------------------------------------
    const tinygltf::Accessor& posAccessor = model.accessors[posIdx];
    size_t vertexCount = posAccessor.count;

    const unsigned char* posPtr = (const unsigned char*)getDataPointer<float>(model, posIdx);
    int posStride = getStride(model, posIdx);

    const unsigned char* normPtr = (const unsigned char*)getDataPointer<float>(model, normIdx);
    int normStride = getStride(model, normIdx);

    const unsigned char* uvPtr = (const unsigned char*)getDataPointer<float>(model, uv0Idx);
    int uvStride = getStride(model, uv0Idx);

    const unsigned char* tanPtr = (const unsigned char*)getDataPointer<float>(model, tanIdx);
    int tanStride = getStride(model, tanIdx);

    subMesh.vertices.resize(vertexCount);

    for (size_t i = 0; i < vertexCount; ++i) {
        Vertex& v = subMesh.vertices[i];

        // Position (Vec3)
        const float* p = (const float*)(posPtr + i * posStride);
        v.position = glm::vec3(p[0], p[1], p[2]);

        // Normal (Vec3)
        if (normPtr) {
            const float* n = (const float*)(normPtr + i * normStride);
            v.normal = glm::vec3(n[0], n[1], n[2]);
        } else {
            v.normal = glm::vec3(0.0f); // 后续可能会重新计算
        }

        // UV (Vec2)
        if (uvPtr) {
            const float* u = (const float*)(uvPtr + i * uvStride);
            // 关键修复：glTF 的 V 轴需要翻转以适配 OpenGL
            v.texCoord = glm::vec2(u[0], 1.0f - u[1]); 
            subMesh.hasUVs = true;
        } else {
            v.texCoord = glm::vec2(0.0f);
        }

        // Tangent (Vec4 usually, but we store Vec3 for now)
        // glTF 的 Tangent 是 Vec4，w 分量用于存储副切线符号
        if (tanPtr) {
            const float* t = (const float*)(tanPtr + i * tanStride);
            // 读取所有 4 个分量
            v.tangent = glm::vec4(t[0], t[1], t[2], t[3]); 
        } else {
            v.tangent = glm::vec4(0.0f);
        }
    }

    // -------------------------------------------------------------
    // 3. 读取索引数据
    // -------------------------------------------------------------
    int indicesIdx = primitive.indices;
    if (indicesIdx >= 0) {
        const tinygltf::Accessor& idxAccessor = model.accessors[indicesIdx];
        
        // 索引可能是 unsigned short, unsigned int, 或者 unsigned byte
        if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            const uint16_t* buf = getDataPointer<uint16_t>(model, indicesIdx);
            int stride = getStride(model, indicesIdx); // Stride usually 2
            
            // 注意：如果 BufferView 紧密排列，可以直接 memcpy，但为了安全处理 stride，我们用循环
            for (size_t i = 0; i < idxAccessor.count; ++i) {
                // 指针运算处理 stride (尽管 indices 通常是紧密的)
                // 简单的转换：const uint16_t* ptr = (const uint16_t*)((const unsigned char*)buf + i * stride);
                // 但 glTF 规范中 index buffer 通常没有 stride (byteStride undefined)
                // 这里我们假设标准情况
                subMesh.indices.push_back((uint32_t)buf[i]); 
            }
        } 
        else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            const uint32_t* buf = getDataPointer<uint32_t>(model, indicesIdx);
            for (size_t i = 0; i < idxAccessor.count; ++i) {
                subMesh.indices.push_back(buf[i]);
            }
        }
        else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            const uint8_t* buf = getDataPointer<uint8_t>(model, indicesIdx);
            for (size_t i = 0; i < idxAccessor.count; ++i) {
                subMesh.indices.push_back((uint32_t)buf[i]);
            }
        }
    } else {
        // 无索引绘制 (很少见，但为了健壮性)
        // 简单的生成 0, 1, 2...
        for(size_t i=0; i<vertexCount; ++i) subMesh.indices.push_back(i);
    }

    // 如果 glTF 文件中没有切线数据，我们需要手动计算
    if (tanIdx < 0 && subMesh.hasUVs) 
    {
        // 使用 GeometryFactory 中的通用算法计算切线
        GeometryFactory::computeTangents(subMesh.vertices, subMesh.indices);
        // 可选：打印一条调试日志
        std::cout << "[GLTF] Computed missing tangents for: " << name << std::endl;
    }

    outMeshes.push_back(std::move(subMesh));
}

// 递归遍历节点
void processNode(const tinygltf::Model& model, const tinygltf::Node& node, std::vector<SubMesh>& outMeshes) {
    // 1. 如果节点包含网格，处理它
    if (node.mesh >= 0) {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        
        // 一个 Mesh 可能包含多个 Primitives (即子网格)
        for (size_t i = 0; i < mesh.primitives.size(); ++i) {
            std::string subName = mesh.name;
            if (subName.empty()) subName = node.name;
            if (subName.empty()) subName = "Mesh_" + std::to_string(node.mesh);
            
            subName += "_" + std::to_string(i); // 区分 primitive

            processPrimitive(model, mesh.primitives[i], subName, outMeshes);
        }
    }

    // 2. 递归处理子节点
    for (int childIdx : node.children) {
        processNode(model, model.nodes[childIdx], outMeshes);
    }
}

std::vector<SubMesh> GLTFLoader::loadScene(const std::string& filepath) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ret = false;
    std::string ext = std::filesystem::path(filepath).extension().string();
    
    // 1. 加载文件
    if (ext == ".glb") {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, filepath);
    } else {
        // 假设是 .gltf
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, filepath);
    }

    if (!warn.empty()) {
        std::cout << "[GLTF Loader] Warn: " << warn << std::endl;
    }

    if (!err.empty()) {
        std::cerr << "[GLTF Loader] Error: " << err << std::endl;
    }

    if (!ret) {
        std::cerr << "[GLTF Loader] Failed to parse glTF: " << filepath << std::endl;
        return {};
    }

    std::vector<SubMesh> meshes;

    // 2. 遍历 Scene
    const tinygltf::Scene& scene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
    
    // 遍历根节点
    for (int nodeIdx : scene.nodes) {
        processNode(model, model.nodes[nodeIdx], meshes);
    }

    std::cout << "[GLTF Loader] Loaded " << meshes.size() << " submeshes from " << filepath << std::endl;

    return meshes;
}