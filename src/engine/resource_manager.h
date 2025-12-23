#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <filesystem>
#include "engine/model.h"
#include "base/texture2d.h"
#include "engine/utils/asset_signature.h"
#include "engine/asset_data.h"

// 场景资源容器
// 它代表一个文件里的所有物体 (例如 "kitchen.obj" 里包含的 Table, Chair, Fridge...)
struct SceneResource {
    struct Node {
        std::string name;             // 子物体名称 (如 "Chair_Leg")
        std::shared_ptr<Model> model; // 对应的 GPU 模型资源
    };
    std::vector<Node> nodes;
};

class ResourceManager
{
public:
    // 单例访问
    static ResourceManager& Get();

    // 设置项目根目录 (比如 "D:/MyGraphicsProject/")
    void setProjectRoot(const std::string& rootPath);
    
    // 获取项目根目录
    std::string getProjectRoot() const { return _projectRoot; }

    // 获取完整路径 (用于加载)
    std::string getFullPath(const std::string& relativePath);

    // 加载或获取已缓存的模型
    // path: 相对路径，例如 "obj/bunny.obj"
    std::shared_ptr<Model> getModel(const std::string& pathKey, bool useFlatShade, const std::string& subMeshName = "");

    // 获取整个场景资源 (通常用于 "Import Scene")
    // 这将加载文件中的所有物体，并自动缓存它们
    std::shared_ptr<SceneResource> getSceneResource(const std::string& pathKey, bool useFlatShade);

    // 加载或获取已缓存的纹理
    std::shared_ptr<ImageTexture2D> getTexture(const std::string& pathKey);

    // 扫描资源目录下所有的 .obj 文件 (用于 UI 显示)
    // rootDir: 资源根目录，例如 "../../media/"
    void scanDirectory(const std::string& rootDir);

    // 获取扫描到的文件列表 (文件名, 相对路径)
    const std::vector<std::pair<std::string, std::string>>& getFileList() const { return _fileList; }

    // 手动将已加载的模型注入缓存
    void injectCache(const std::string& pathKey, const std::string& subMeshName, bool useFlatShade, std::shared_ptr<Model> model);

    // 仅检查缓存中是否存在，不触发加载
    std::shared_ptr<Model> findModel(const std::string& pathKey, bool useFlatShade, const std::string& subMeshName = "");

    // 加载原始 HDR 数据
    HDRData loadHDRRaw(const std::string& pathKey);

    // 释放 HDR 内存
    static void freeHDRRaw(HDRData& data);

    void shutdown();

private:
    ResourceManager() = default;

    std::string _projectRoot = ""; // 默认为空

    template <typename T>
    struct CacheEntry {
        std::shared_ptr<T> resource;    // 实际资源
        AssetSignature signature;       // 加载时的版本指纹
        std::string sourcePath;         // 原始绝对路径 (用于重校验)
    };

    // 模型缓存：key=相对路径, value=模型指针
    std::unordered_map<std::string, CacheEntry<Model>> _modelCache;

    // 场景资源缓存
    std::unordered_map<std::string, CacheEntry<SceneResource>> _sceneCache;

    // 纹理缓存
    std::unordered_map<std::string, CacheEntry<ImageTexture2D>> _textureCache;

    // 扫描到的文件列表
    std::vector<std::pair<std::string, std::string>> _fileList;
};