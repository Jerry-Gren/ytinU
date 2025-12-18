#pragma once

#include <vector>
#include <memory>
#include <algorithm>
#include "scene_object.h" // 根据你的实际路径调整
#include "geometry_factory.h"

class Scene
{
public:
    Scene() = default;
    ~Scene() = default;

    // --- 对象管理 ---
    
    // 获取所有对象 (供 Renderer 遍历)
    const std::vector<std::unique_ptr<GameObject>>& getGameObjects() const { return _gameObjects; }

    // 添加一个已经创建好的对象
    void addGameObject(std::unique_ptr<GameObject> go) {
        _gameObjects.push_back(std::move(go));
    }

    // 删除指定对象
    void removeGameObject(GameObject* go) {
        _gameObjects.erase(
            std::remove_if(_gameObjects.begin(), _gameObjects.end(),
                [go](const std::unique_ptr<GameObject>& p) { return p.get() == go; }),
            _gameObjects.end());
    }

    // 清空场景
    void clear() { _gameObjects.clear(); }

    // --- 工厂方法 (从 SceneRoaming 迁移过来的逻辑) ---
    
    // 创建默认立方体
    GameObject* createCube();
    
    // 创建点光源
    GameObject* createPointLight();

    // 创建默认场景 (比如初始化一个太阳)
    void createDefaultScene();

    void markForDestruction(GameObject* go);

    bool isMarkedForDestruction(GameObject* go) const {
        if (!go) return false;
        // 检查指针是否存在于 _killQueue 中
        return std::find(_killQueue.begin(), _killQueue.end(), go) != _killQueue.end();
    }

    void destroyMarkedObjects();

private:
    std::vector<std::unique_ptr<GameObject>> _gameObjects;

    std::vector<GameObject*> _killQueue;
};