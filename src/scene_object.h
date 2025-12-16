#pragma once

#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <iostream>

#include "base/transform.h"
#include "model.h"
#include "light_structs.h"

enum class MeshShapeType
{
    Cube,
    Sphere,
    Cylinder,
    Cone,
    Prism,   // 多面棱柱
    Frustum, // 多面棱台
    Plane,
    CustomOBJ // 自定义 OBJ 文件
};

// 定义一个结构体来保存生成参数，防止每次切换丢失
struct MeshParams
{
    // 通用
    float size = 1.0f;
    float radius = 0.5f;
    float height = 1.0f;

    // 平面
    float width = 10.0f;
    float depth = 10.0f;

    // 圆柱/球/圆锥
    int slices = 32;
    int stacks = 16;

    // 棱柱/棱台
    float topRadius = 0.5f;
    float bottomRadius = 1.0f;
    int sides = 6; // 默认六棱柱

    // OBJ
    char objPath[256] = "";
};

// 前置声明
class GameObject;

// ==========================================
// 组件类型枚举 (用于运行时识别，替代复杂的 dynamic_cast)
// ==========================================
enum class ComponentType
{
    MeshRenderer,
    Light
};

// ==========================================
// 1. 组件基类
// ==========================================
class Component
{
public:
    GameObject *owner = nullptr;
    bool enabled = true;

    virtual ~Component() = default;

    // 纯虚函数：获取类型
    virtual ComponentType getType() const = 0;
};

// ==========================================
// 2. 网格渲染组件
// ==========================================
class MeshComponent : public Component
{
public:
    std::shared_ptr<Model> model;
    Material material;

    // 是否是Gizmo (编辑器辅助物体，如灯泡图标)，渲染时不受光照影响
    bool isGizmo = false;
    
    // 是否双面渲染 (默认 false，Plane 需要设为 true)
    bool doubleSided = false;

    MeshShapeType shapeType = MeshShapeType::Cube;
    MeshParams params;

    MeshComponent(std::shared_ptr<Model> m, bool gizmo = false)
        : model(m), isGizmo(gizmo) {}

    ComponentType getType() const override { return ComponentType::MeshRenderer; }

    void setMesh(std::shared_ptr<Model> newModel)
    {
        if (newModel) model = newModel; // shared_ptr 赋值会自动处理引用计数
    }
};

// ==========================================
// 3. 光照组件
// ==========================================
enum class LightType
{
    Directional,
    Point,
    Spot
};

class LightComponent : public Component
{
public:
    LightType type;
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;

    // 衰减
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;

    // 聚光
    float cutOff = glm::cos(glm::radians(12.5f));
    float outerCutOff = glm::cos(glm::radians(17.5f));

    LightComponent(LightType t) : type(t) {}

    ComponentType getType() const override { return ComponentType::Light; }
};

// ==========================================
// 4. 游戏对象
// ==========================================
class GameObject
{
public:
    std::string name;
    Transform transform;
    std::vector<std::unique_ptr<Component>> components;

    GameObject(const std::string &n) : name(n) {}

    template <typename T, typename... Args>
    T *addComponent(Args &&...args)
    {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        comp->owner = this;
        T *ptr = comp.get();
        components.push_back(std::move(comp));
        return ptr;
    }

    template <typename T>
    T *getComponent()
    {
        for (auto &comp : components)
        {
            if (dynamic_cast<T *>(comp.get()))
            {
                return static_cast<T *>(comp.get());
            }
        }
        return nullptr;
    }

    void removeComponent(Component *comp)
    {
        components.erase(
            std::remove_if(components.begin(), components.end(),
                           [comp](const std::unique_ptr<Component> &p)
                           { return p.get() == comp; }),
            components.end());
    }
};
