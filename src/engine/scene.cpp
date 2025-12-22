#include "scene.h"
#include "resource_manager.h" // 如果需要加载默认图标
#include "obj_loader.h"

#include <fstream>
#include <iomanip>
#include <iostream>

GameObject* Scene::createCube()
{
    auto go = new GameObject("Cube");
    // 使用 GeometryFactory 创建
    auto meshComp = go->addComponent<MeshComponent>(GeometryFactory::createCube());
    
    // 初始化默认参数
    meshComp->material.albedo = glm::vec3(0.8f); 
    meshComp->material.roughness = 0.5f;
    meshComp->material.metallic = 0.0f;
    meshComp->material.ao = 1.0f;

    // 存入容器
    _gameObjects.push_back(std::unique_ptr<GameObject>(go));
    return go;
}

GameObject* Scene::createPointLight()
{
    auto go = new GameObject("Point Light");
    auto lightComp = go->addComponent<LightComponent>(LightType::Point);
    lightComp->color = glm::vec3(1.0f, 1.0f, 0.0f);
    lightComp->range = 10.0f;

    // 光源可视化 (Gizmo)
    auto meshComp = go->addComponent<MeshComponent>(GeometryFactory::createSphere(0.2f), true);
    meshComp->shapeType = MeshShapeType::Sphere;
    meshComp->params.radius = 0.2f;
    meshComp->material.albedo = lightComp->color;

    _gameObjects.push_back(std::unique_ptr<GameObject>(go));
    return go;
}

void Scene::createDefaultScene()
{
    // 创建默认的平行光 (Sun)
    auto sun = new GameObject("Directional Light");
    auto lightComp = sun->addComponent<LightComponent>(LightType::Directional);
    
    sun->transform.rotationEuler = glm::vec3(-50.0f, -30.0f, 0.0f);
    sun->transform.setRotation(sun->transform.rotationEuler);

    // 尝试加载 Gizmo 图标
    try {
        std::string arrowPath = "media/obj/arrow.obj";
        auto arrowModel = ResourceManager::Get().getModel(arrowPath, false);
        if (arrowModel) {
            auto arrowMesh = sun->addComponent<MeshComponent>(arrowModel, true);
            arrowMesh->shapeType = MeshShapeType::CustomOBJ;
            strcpy(arrowMesh->params.objPath, arrowPath.c_str());
            sun->transform.scale = glm::vec3(0.5f);
        }
    } catch (...) {}

    _gameObjects.push_back(std::unique_ptr<GameObject>(sun));
}

void Scene::markForDestruction(GameObject* go)
{
    // 检查是否已经在队列中，防止重复添加
    if (std::find(_killQueue.begin(), _killQueue.end(), go) == _killQueue.end()) {
        _killQueue.push_back(go);
    }
}

void Scene::destroyMarkedObjects()
{
    if (_killQueue.empty()) return;

    for (GameObject* go : _killQueue)
    {
        // 执行真正的物理删除
        _gameObjects.erase(
            std::remove_if(_gameObjects.begin(), _gameObjects.end(),
                [go](const std::unique_ptr<GameObject>& p) { 
                    return p.get() == go; 
                }),
            _gameObjects.end());
    }
    _killQueue.clear();
}

void Scene::exportToOBJ(const std::string& filename)
{
    std::ofstream out(filename);
    if (!out.is_open())
    {
        std::cerr << "Failed to open file for export: " << filename << std::endl;
        return;
    }

    out << "# Exported Scene\n";
    
    // 全局顶点偏移量 (OBJ 索引从 1 开始)
    uint32_t globalVertexOffset = 1; 

    for (const auto& go : _gameObjects)
    {
        // 1. 获取网格组件
        auto meshComp = go->getComponent<MeshComponent>();
        if (!meshComp || !meshComp->enabled || !meshComp->model) continue;

        const auto& vertices = meshComp->model->getVertices();
        const auto& indices = meshComp->model->getIndices();

        if (vertices.empty() || indices.empty()) continue;

        // 写入对象名称
        out << "o " << go->name << "_" << go->getInstanceID() << "\n";

        // 2. 计算变换矩阵
        glm::mat4 modelMat = go->transform.getLocalMatrix();
        // 还要叠加上 Model 自身的变换 (如果有的话，通常 Model 自带变换是单位矩阵，但也可能不是)
        modelMat = modelMat * meshComp->model->transform.getLocalMatrix();

        // 法线矩阵 (用于正确变换法线，处理非均匀缩放)
        glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(modelMat)));

        // 3. 写入顶点数据 (v, vt, vn)
        for (const auto& v : vertices)
        {
            // --- 顶点位置 (转换到世界空间) ---
            glm::vec4 worldPos = modelMat * glm::vec4(v.position, 1.0f);
            out << "v " << std::fixed << std::setprecision(6) 
                << worldPos.x << " " << worldPos.y << " " << worldPos.z << "\n";

            // --- 纹理坐标 (直接输出) ---
            out << "vt " << v.texCoord.x << " " << v.texCoord.y << "\n";

            // --- 法线 (转换到世界空间并归一化) ---
            glm::vec3 worldNorm = glm::normalize(normalMat * v.normal);
            out << "vn " << worldNorm.x << " " << worldNorm.y << " " << worldNorm.z << "\n";
        }

        // 4. 写入面数据 (f v/vt/vn)
        // 注意：OBJ 索引是从 1 开始的全局索引
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            uint32_t idx0 = indices[i]     + globalVertexOffset;
            uint32_t idx1 = indices[i + 1] + globalVertexOffset;
            uint32_t idx2 = indices[i + 2] + globalVertexOffset;

            // 格式: f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3
            // 这里我们假设 v, vt, vn 的索引是相同的 (因为我们的 Mesh 结构体就是这样存的)
            out << "f " 
                << idx0 << "/" << idx0 << "/" << idx0 << " "
                << idx1 << "/" << idx1 << "/" << idx1 << " "
                << idx2 << "/" << idx2 << "/" << idx2 << "\n";
        }

        // 更新偏移量
        globalVertexOffset += (uint32_t)vertices.size();
    }

    out.close();
    std::cout << "Scene exported successfully to " << filename << std::endl;
}

void Scene::importSceneFromOBJ(const std::string& filepath)
{
    // 1. 调用多网格加载器
    // 这里的 false 表示不强制 Split Vertices (Smooth Shading)，保留 OBJ 原貌
    // 如果你希望导入的模型默认都是 Low-Poly 风格，可以传 true
    std::vector<SubMesh> meshes;
    try {
        meshes = OBJLoader::loadScene(filepath, false);
    }
    catch (const std::exception& e) {
        std::cerr << "[Scene] Import failed: " << e.what() << std::endl;
        return;
    }

    if (meshes.empty()) {
        std::cout << "[Scene] No meshes found in " << filepath << std::endl;
        return;
    }

    // 2. 为每个 SubMesh 创建 GameObject
    for (const auto& subMesh : meshes)
    {
        // 创建 GameObject
        // 注意：subMesh.name 来自 OBJ 文件里的 'o' 或 'g' 标签
        auto go = new GameObject(subMesh.name);

        // 创建 Model
        // 我们利用现有的 Model 构造函数 (从顶点/索引数组构建)
        // 注意：Model 内部会重新计算包围盒
        auto model = std::make_shared<Model>(subMesh.vertices, subMesh.indices);
        
        // 添加 MeshComponent
        auto meshComp = go->addComponent<MeshComponent>(model);
        meshComp->shapeType = MeshShapeType::CustomOBJ;
        
        // 记录来源路径
        // 虽然这个路径指向的是整个场景文件，但作为元数据保留是有用的
        strncpy(meshComp->params.objPath, filepath.c_str(), sizeof(meshComp->params.objPath) - 1);
        meshComp->params.objPath[sizeof(meshComp->params.objPath) - 1] = '\0';
        
        // [智能 UV 检测]
        // 如果 SubMesh 标记没有 UV，自动开启 Triplanar Mapping
        // 这样导入的无 UV 模型（如简单的几何体组合）也能直接贴图
        if (!subMesh.hasUVs) {
            meshComp->useTriplanar = true;
            meshComp->triplanarScale = 0.2f; // 默认缩放，可视情况调整
        } else {
            meshComp->useTriplanar = false;
        }

        // 将新创建的物体加入场景列表
        _gameObjects.push_back(std::unique_ptr<GameObject>(go));
    }
    
    std::cout << "[Scene] Imported " << meshes.size() << " objects from " << filepath << std::endl;
}

void Scene::importSingleMeshFromOBJ(const std::string& filepath)
{
    // 1. 从路径提取文件名作为物体名称 (例如 "C:/Assets/Chair.obj" -> "Chair")
    std::string name = std::filesystem::path(filepath).stem().string();
    if (name.empty()) name = "Imported Mesh";

    // 2. 加载网格数据 (Flatten 模式，不拆分物体)
    MeshData meshData;
    try {
        // false = 不强制 Flat Shading (默认平滑)
        meshData = OBJLoader::load(filepath, false);
    }
    catch (const std::exception& e) {
        std::cerr << "[Scene] Failed to load mesh: " << e.what() << std::endl;
        return;
    }

    if (meshData.vertices.empty()) {
        std::cerr << "[Scene] Mesh is empty: " << filepath << std::endl;
        return;
    }

    // 3. 创建 GameObject
    auto go = new GameObject(name);

    // 4. 创建 Model
    // 使用顶点和索引数组构建 Model
    auto model = std::make_shared<Model>(meshData.vertices, meshData.indices);

    // 5. 添加 MeshComponent
    auto meshComp = go->addComponent<MeshComponent>(model);
    
    // 设置类型为 CustomOBJ，这样 Inspector 会显示文件路径槽
    meshComp->shapeType = MeshShapeType::CustomOBJ;

    // 6. 记录文件路径
    strncpy(meshComp->params.objPath, filepath.c_str(), sizeof(meshComp->params.objPath) - 1);
    meshComp->params.objPath[sizeof(meshComp->params.objPath) - 1] = '\0';

    // 7. [智能 UV 检测]
    // 如果加载的数据里没有 UV，自动开启 Triplanar Mapping
    if (!meshData.hasUVs) {
        meshComp->useTriplanar = true;
        meshComp->triplanarScale = 0.2f; // 默认缩放值，可视情况调整
    } else {
        meshComp->useTriplanar = false;
    }

    // 8. 加入场景列表
    _gameObjects.push_back(std::unique_ptr<GameObject>(go));

    std::cout << "[Scene] Imported single mesh: " << name << std::endl;
}