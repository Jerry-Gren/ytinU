#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <filesystem>
#include "engine/model.h"
#include "base/texture2d.h"

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
    std::shared_ptr<Model> getModel(const std::string& pathKey, bool useFlatShade);

    // 加载或获取已缓存的纹理
    std::shared_ptr<ImageTexture2D> getTexture(const std::string& pathKey);

    // 扫描资源目录下所有的 .obj 文件 (用于 UI 显示)
    // rootDir: 资源根目录，例如 "../../media/"
    void scanDirectory(const std::string& rootDir);

    // 获取扫描到的文件列表 (文件名, 相对路径)
    const std::vector<std::pair<std::string, std::string>>& getFileList() const { return _fileList; }

    void shutdown() {
        _modelCache.clear(); // 强制释放所有 Model shared_ptr
        _textureCache.clear();
    }

private:
    ResourceManager() = default;

    std::string _projectRoot = ""; // 默认为空

    // 模型缓存：key=相对路径, value=模型指针
    std::unordered_map<std::string, std::shared_ptr<Model>> _modelCache;

    // 纹理缓存
    std::unordered_map<std::string, std::shared_ptr<ImageTexture2D>> _textureCache;

    // 扫描到的文件列表
    std::vector<std::pair<std::string, std::string>> _fileList;
};