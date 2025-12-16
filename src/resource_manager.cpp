#include "resource_manager.h"
#include <iostream>

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
    if (!fs::exists(rootDir)) return;

    for (const auto& entry : fs::recursive_directory_iterator(rootDir))
    {
        if (entry.is_regular_file())
        {
            std::string ext = entry.path().extension().string();
            // 转小写比较...
            if (ext == ".obj" || ext == ".OBJ")
            {
                std::string fullPath = entry.path().string();
                std::string filename = entry.path().filename().string();
                
                // [关键] 我们计算相对路径存起来，方便存盘
                // 这里简单存 filename 也可以，或者 path relative_to root
                std::string relPath = fs::relative(entry.path(), rootDir).string();
                
                _fileList.push_back({filename, relPath});
            }
        }
    }
}

std::shared_ptr<Model> ResourceManager::getModel(const std::string& relPath)
{
    // 1. 检查缓存 (Key 是相对路径，比如 "assets/sphere.obj")
    auto it = _modelCache.find(relPath);
    if (it != _modelCache.end())
    {
        return it->second;
    }

    // 2. 缓存未命中，准备加载
    // [核心修复] 获取硬盘上的绝对路径
    std::string fullPath = getFullPath(relPath);

    // [调试] 打印一下路径，确认拼对了吗
    // std::cout << "Loading Model: " << fullPath << std::endl;

    try {
        // 3. 使用【绝对路径】去打开文件
        std::shared_ptr<Model> newModel = std::make_shared<Model>(fullPath);
        
        // 4. 存入缓存 (Key 依然是【相对路径】，方便下次查找)
        _modelCache[relPath] = newModel; 
        
        return newModel;
    }
    catch (std::exception& e) {
        std::cerr << "ResourceManager Failed: " << e.what() << std::endl;
        return nullptr;
    }
}