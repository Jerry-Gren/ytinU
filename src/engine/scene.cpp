#include "scene.h"
#include "resource_manager.h" // 如果需要加载默认图标

GameObject* Scene::createCube()
{
    auto go = new GameObject("Cube");
    // 使用 GeometryFactory 创建
    auto meshComp = go->addComponent<MeshComponent>(GeometryFactory::createCube());
    
    // 初始化默认参数
    meshComp->shapeType = MeshShapeType::Cube;
    meshComp->params.size = 1.0f;
    meshComp->material.diffuse = glm::vec3(0.8f);

    // 存入容器
    _gameObjects.push_back(std::unique_ptr<GameObject>(go));
    return go;
}

GameObject* Scene::createPointLight()
{
    auto go = new GameObject("Point Light");
    auto lightComp = go->addComponent<LightComponent>(LightType::Point);
    lightComp->color = glm::vec3(1.0f, 1.0f, 0.0f);

    // 光源可视化 (Gizmo)
    auto meshComp = go->addComponent<MeshComponent>(GeometryFactory::createSphere(0.2f), true);
    meshComp->shapeType = MeshShapeType::Sphere;
    meshComp->params.radius = 0.2f;
    meshComp->material.diffuse = lightComp->color;

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
        std::string arrowPath = "obj/arrow.obj"; 
        auto arrowModel = ResourceManager::Get().getModel(arrowPath);
        if (arrowModel) {
            auto arrowMesh = sun->addComponent<MeshComponent>(arrowModel, true);
            arrowMesh->shapeType = MeshShapeType::CustomOBJ;
            strcpy(arrowMesh->params.objPath, arrowPath.c_str());
            sun->transform.scale = glm::vec3(0.5f); 
        }
    } catch (...) {}

    _gameObjects.push_back(std::unique_ptr<GameObject>(sun));
}