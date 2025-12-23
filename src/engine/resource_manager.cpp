#include "resource_manager.h"
#include "obj_loader.h"
#include <iostream>
#include <algorithm>
#include <stb_image.h>

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

    shutdown();

    // 设置完路径后，立即重新扫描
    scanDirectory(_projectRoot);
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
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            bool isModel = (ext == ".obj");
            bool isTexture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                              ext == ".bmp" || ext == ".tga" || ext == ".hdr");

            if (isModel || isTexture)
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

std::shared_ptr<Model> ResourceManager::getModel(const std::string& pathKey, bool useFlatShade, const std::string& subMeshName)
{
    std::string cleanPath = pathKey;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    std::string cacheKey = cleanPath;
    if (useFlatShade) cacheKey += ":useFlatShade";
    if (!subMeshName.empty()) cacheKey += ":" + subMeshName;

    // 1. 获取绝对路径 (用于检测文件变化)
    std::string fullPath = getFullPath(cleanPath);

    // 2. 检查缓存
    auto it = _modelCache.find(cacheKey);
    if (it != _modelCache.end())
    {
        // 命中缓存后，检查文件是否被修改
        // 生成当前磁盘文件的签名
        AssetSignature currentSig = AssetSignature::generate(fullPath);

        // 如果签名不一致，则缓存失效
        if (currentSig != it->second.signature) {
            std::cout << "[ResourceManager] Hot-Reload Detected: " << cleanPath << std::endl;
            // 不需要手动 erase，下面的加载逻辑会覆盖它
        } 
        else {
            // 签名一致，直接返回缓存
            return it->second.resource;
        }
    }

    // 3. 缓存未命中，准备加载
    if (!std::filesystem::exists(fullPath)) {
        std::cerr << "[ResourceManager] Error: File not found: " << fullPath << std::endl;
        return nullptr; // 或者返回一个紫黑格子的 "ErrorModel"
    }

    // [调试] 打印一下路径，确认拼对了吗
    // std::cout << "Loading Model: " << fullPath << std::endl;

    try {
        // 加载数据
        MeshData data = OBJLoader::load(fullPath, useFlatShade, subMeshName);
        if (data.vertices.empty()) return nullptr;

        // 创建 GPU 资源
        std::shared_ptr<Model> newModel = std::make_shared<Model>(data.vertices, data.indices);

        // 4. 构建新的缓存条目
        CacheEntry<Model> entry;
        entry.resource = newModel;
        entry.sourcePath = fullPath;
        entry.signature = AssetSignature::generate(fullPath); // 记录当前版本
        
        // 存入 Map
        _modelCache[cacheKey] = entry;
        return newModel;
    }
    catch (std::exception& e) {
        std::cerr << "[ResourceManager] Failed to load model: " << e.what() << std::endl;
        return nullptr;
    }
}

std::shared_ptr<SceneResource> ResourceManager::getSceneResource(const std::string& pathKey, bool useFlatShade)
{
    // 1. 路径标准化
    std::string cleanPath = pathKey;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    // 2. 生成 Cache Key
    // 场景加载只受路径和平滑模式影响，不受 subMeshName 影响
    std::string cacheKey = cleanPath;
    if (useFlatShade) cacheKey += ":useFlatShade";

    // 3. 获取绝对路径
    std::string fullPath = getFullPath(cleanPath);

    // 4. 检查缓存 (Dirty Check)
    auto it = _sceneCache.find(cacheKey);
    if (it != _sceneCache.end())
    {
        AssetSignature currentSig = AssetSignature::generate(fullPath);
        if (currentSig != it->second.signature) {
            std::cout << "[ResourceManager] Hot-Reload Detected (Scene): " << cleanPath << std::endl;
            // 缓存已脏，继续执行加载逻辑
        } 
        else {
            return it->second.resource;
        }
    }

    if (!std::filesystem::exists(fullPath)) {
        std::cerr << "[ResourceManager] Error: Scene file not found: " << fullPath << std::endl;
        return nullptr;
    }

    try {
        // 这将返回 raw CPU data (vector<SubMesh>)
        std::vector<SubMesh> subMeshes = OBJLoader::loadScene(fullPath, useFlatShade);
        
        if (subMeshes.empty()) return nullptr;

        // 创建新的场景资源容器
        auto newSceneRes = std::make_shared<SceneResource>();
        AssetSignature fileSig = AssetSignature::generate(fullPath);

        // 遍历加载到的子网格，转换为 GPU Model
        for (const auto& sub : subMeshes)
        {
            // 1. 构建 Model
            auto model = std::make_shared<Model>(sub.vertices, sub.indices);
            
            // 2. 添加到 SceneResource
            newSceneRes->nodes.push_back({ sub.name, model });

            // 3. [自动缓存注入] 
            // 将这个子模型单独注册到 _modelCache 中
            // 这样 getModel("file.obj", ..., "SubName") 也能直接命中
            std::string modelCacheKey = cacheKey + ":" + sub.name;
            
            CacheEntry<Model> subEntry;
            subEntry.resource = model;
            subEntry.sourcePath = fullPath;
            subEntry.signature = fileSig; // 共享同一个文件的签名
            
            _modelCache[modelCacheKey] = subEntry;
        }

        // 如果场景只有一个物体，我们也注册一个“默认单体”缓存 (空 subMeshName)
        if (subMeshes.size() == 1) {
            CacheEntry<Model> singleEntry;
            singleEntry.resource = newSceneRes->nodes[0].model;
            singleEntry.sourcePath = fullPath;
            singleEntry.signature = fileSig;
            _modelCache[cacheKey] = singleEntry; // cacheKey 就是不带 subName 的 key
        }

        // 5. 存入 Scene 缓存
        CacheEntry<SceneResource> entry;
        entry.resource = newSceneRes;
        entry.sourcePath = fullPath;
        entry.signature = fileSig;

        _sceneCache[cacheKey] = entry;

        return newSceneRes;
    }
    catch (std::exception& e) {
        std::cerr << "[ResourceManager] Failed to load scene: " << e.what() << std::endl;
        return nullptr;
    }
}

std::shared_ptr<ImageTexture2D> ResourceManager::getTexture(const std::string& pathKey)
{
    std::string cleanPath = pathKey;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    std::string cacheKey = cleanPath;

    // 1. 获取绝对路径 (用于检测文件变化)
    std::string fullPath = getFullPath(cleanPath);

    // 2. 检查缓存
    auto it = _textureCache.find(cacheKey);
    if (it != _textureCache.end()) {
        AssetSignature currentSig = AssetSignature::generate(fullPath);
        // 对比签名
        if (currentSig != it->second.signature) {
            std::cout << "[ResourceManager] Hot-Reload Detected: " << cleanPath << std::endl;
            // 签名不一致，继续向下执行加载逻辑（覆盖旧缓存）
        } else {
            return it->second.resource;
        }
    }

    // 3. 缓存未命中，准备加载
    if (!std::filesystem::exists(fullPath)) {
        std::cerr << "[ResourceManager] Error: Texture not found: " << fullPath << std::endl;
        return nullptr;
    }

    try {
        auto newTex = std::make_shared<ImageTexture2D>(fullPath);
        
        CacheEntry<ImageTexture2D> entry;
        entry.resource = newTex;
        entry.sourcePath = fullPath;
        entry.signature = AssetSignature::generate(fullPath);

        // 4. 存入缓存
        _textureCache[cacheKey] = entry;
        return newTex;
    }
    catch (std::exception& e) {
        std::cerr << "[ResourceManager] Failed to load texture: " << e.what() << std::endl;
        return nullptr;
    }
}

void ResourceManager::injectCache(const std::string& pathKey, const std::string& subMeshName, bool useFlatShade, std::shared_ptr<Model> model)
{
    if (!model) return;

    // 1. 标准化路径
    std::string cleanPath = pathKey;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    // 2. 生成 Cache Key
    std::string cacheKey = cleanPath;
    if (useFlatShade) cacheKey += ":useFlatShade";
    if (!subMeshName.empty()) cacheKey += ":" + subMeshName;

    // 构造 Entry
    CacheEntry<Model> entry;
    entry.resource = model;
    entry.sourcePath = getFullPath(cleanPath); // 尝试补全绝对路径
    entry.signature = AssetSignature::generate(entry.sourcePath); // 生成签名

    // 3. 存入缓存
    _modelCache[cacheKey] = entry;
    
    std::cout << "[ResourceManager] Injected cache: " << cacheKey << std::endl;
}

std::shared_ptr<Model> ResourceManager::findModel(const std::string& pathKey, bool useFlatShade, const std::string& subMeshName)
{
    std::string cleanPath = pathKey;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    std::string cacheKey = cleanPath;
    if (useFlatShade) cacheKey += ":useFlatShade";
    if (!subMeshName.empty()) cacheKey += ":" + subMeshName;

    auto it = _modelCache.find(cacheKey);
    if (it != _modelCache.end()) {
        // findModel 只是为了查询是否存在内存副本，通常不需要做 Dirty Check (性能优先)
        // 或者是为了 Scene 逻辑复用，如果我们要严格一点，这里也可以加 check
        return it->second.resource;
    }
    return nullptr;
}

HDRData ResourceManager::loadHDRRaw(const std::string& pathKey)
{
    std::string cleanPath = pathKey;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');
    std::string fullPath = getFullPath(cleanPath);

    HDRData result;
    
    // 翻转 Y 轴 (通常 OpenGL 纹理都需要翻转，除非 Shader 里处理了)
    stbi_set_flip_vertically_on_load(true);
    
    result.data = stbi_loadf(fullPath.c_str(), &result.width, &result.height, &result.components, 3); // 强制 3 通道 (RGB)
    
    if (!result.data) {
        std::cerr << "[ResourceManager] Failed to load HDR: " << fullPath << std::endl;
    } else {
        std::cout << "[ResourceManager] Loaded HDR: " << result.width << "x" << result.height << std::endl;
    }

    return result;
}

void ResourceManager::freeHDRRaw(HDRData& data)
{
    if (data.data) {
        stbi_image_free(data.data);
        data.data = nullptr;
    }
}

void ResourceManager::shutdown() {
    _modelCache.clear();
    _sceneCache.clear();
    _textureCache.clear();
}