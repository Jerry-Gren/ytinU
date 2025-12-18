#include "resource_manager.h"
#include <iostream>
#include <algorithm>

ResourceManager& ResourceManager::Get()
{
    static ResourceManager instance;
    return instance;
}

void ResourceManager::setProjectRoot(const std::string& rootPath)
{
    _projectRoot = rootPath;
    
    // 确保路径以分隔符结尾 (Windows '\' 或 Linux '/')
    if (!_projectRoot.empty() && _projectRoot.back() != '/' && _projectRoot.back() != '\\') {
        _projectRoot += "/";
    }

    // 设置完路径后，立即重新扫描
    scanDirectory(_projectRoot);
    
    // 清空旧缓存 (可选，切换项目时应该清空)
    _modelCache.clear();
}

std::string ResourceManager::getFullPath(const std::string& relativePath)
{
    // 如果已经是绝对路径，直接返回
    if (std::filesystem::path(relativePath).is_absolute()) {
        return relativePath;
    }
    return _projectRoot + relativePath;
}

// scanDirectory 里的逻辑稍微改一下，确保存储的是“相对路径”
void ResourceManager::scanDirectory(const std::string& rootDir)
{
    _fileList.clear();
    namespace fs = std::filesystem;
    if (rootDir.empty() || !fs::exists(rootDir) || !fs::is_directory(rootDir)) return;

    for (const auto& entry : fs::recursive_directory_iterator(rootDir))
    {
        if (entry.is_regular_file())
        {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), 
                           [](unsigned char c){ return std::tolower(c); });
            if (ext == ".obj")
            {
                std::string filename = entry.path().filename().string();
                
                std::string storePath;
                try {
                    // 尝试计算相对路径
                    storePath = fs::relative(entry.path(), rootDir).string();
                } catch (const fs::filesystem_error& e) {
                    // 如果无法计算相对路径（比如跨盘符），则退化为存储绝对路径
                    storePath = entry.path().string();
                }
                std::replace(storePath.begin(), storePath.end(), '\\', '/');
                
                _fileList.push_back({filename, storePath});
            }
        }
    }
}

std::shared_ptr<Model> ResourceManager::getModel(const std::string& pathKey, bool useFlatShade)
{
    std::string cleanPath = pathKey;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    std::string cacheKey = cleanPath;
    if (useFlatShade) cacheKey += ":useFlatShade";

    // 1. 检查缓存 (Key 是相对路径，比如 "assets/sphere.obj")
    auto it = _modelCache.find(cacheKey);
    if (it != _modelCache.end())
    {
        return it->second;
    }

    // 2. 缓存未命中，准备加载
    // [核心修复] 获取硬盘上的绝对路径
    std::string fullPath = getFullPath(cleanPath);

    if (!std::filesystem::exists(fullPath)) {
        std::cerr << "[ResourceManager] Error: File not found: " << fullPath << std::endl;
        return nullptr; // 或者返回一个紫黑格子的 "ErrorModel"
    }

    // [调试] 打印一下路径，确认拼对了吗
    // std::cout << "Loading Model: " << fullPath << std::endl;

    try {
        // 3. 使用【绝对路径】去打开文件
        std::shared_ptr<Model> newModel = std::make_shared<Model>(fullPath, useFlatShade);
        
        // 4. 存入缓存 (Key 依然是【相对路径】，方便下次查找)
        _modelCache[cacheKey] = newModel;
        
        return newModel;
    }
    catch (std::exception& e) {
        std::cerr << "[ResourceManager] Failed to load model: " << e.what() << std::endl;
        return nullptr;
    }
}