#pragma once

#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <iostream>
#include <atomic>

#include "base/transform.h"
#include "base/texture2d.h"
#include "engine/model.h"
#include "light_structs.h"

// 前置声明
class GameObject;

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

// ==========================================
// 组件类型枚举 (用于运行时识别，替代复杂的 dynamic_cast)
// ==========================================
enum class ComponentType
{
    MeshRenderer,
    Light,
    ReflectionProbe
};

enum class LightType
{
    Directional,
    Point,
    Spot
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

    // 子网格名称过滤器
    // 如果为空，加载整个文件；如果不为空，只加载匹配该名称的 group/object/material
    char subMeshName[128] = "";
};

// ==========================================
// 0. 辅助：ID 生成器
// ==========================================
class IDGenerator {
public:
    static int generate();
};

// ==========================================
// 1. 组件基类
// ==========================================
class Component
{
public:
    GameObject *owner = nullptr;
    bool enabled = true;

    Component();

    virtual ~Component() = default;

    int getInstanceID() const { return _instanceId; }

    // 纯虚函数：获取类型
    virtual ComponentType getType() const = 0;

protected:
    int _instanceId;
};

// ==========================================
// 2. 网格渲染组件
// ==========================================
class MeshComponent : public Component
{
public:
    static constexpr ComponentType Type = ComponentType::MeshRenderer;

    std::shared_ptr<Model> model;
    std::shared_ptr<ImageTexture2D> diffuseMap; // 漫反射贴图
    std::shared_ptr<ImageTexture2D> normalMap; // 法线贴图
    std::shared_ptr<ImageTexture2D> ormMap; // ORM 贴图 (R=AO, G=Roughness, B=Metallic)
    
    // 优先级逻辑：独立贴图 > ORM贴图 > 纯数值
    std::shared_ptr<ImageTexture2D> aoMap;        // 独立环境光遮蔽 (Ambient Occlusion)
    std::shared_ptr<ImageTexture2D> roughnessMap; // 独立粗糙度 (Roughness)
    std::shared_ptr<ImageTexture2D> metallicMap;  // 独立金属度 (Metallic)

    std::shared_ptr<ImageTexture2D> emissiveMap; // 自发光贴图
    std::shared_ptr<ImageTexture2D> opacityMap; // 透明度贴图
    Material material;

    // 是否是Gizmo (编辑器辅助物体，如灯泡图标)，渲染时不受光照影响
    bool isGizmo = false;
    
    // 是否双面渲染 (默认 false，Plane 需要设为 true)
    bool doubleSided = false;

    // 是否使用硬棱角
    bool useFlatShade = false;

    // Texture设置
    bool useTriplanar = false; // 是否开启三向映射
    float triplanarScale = 1.0f; // 纹理平铺缩放大小
    bool triFlipPosX = false; 
    bool triFlipNegX = false;
    bool triFlipPosY = false;
    bool triFlipNegY = false;
    bool triFlipPosZ = false;
    bool triFlipNegZ = false;
    float triRotPosX = 0.0f;
    float triRotPosY = 0.0f;
    float triRotPosZ = 0.0f;
    float triRotNegX = 0.0f;
    float triRotNegY = 0.0f;
    float triRotNegZ = 0.0f;

    // 法线贴图设置
    float normalStrength = 1.0f;
    bool flipNormalY = false;

    // 自发光贴图设置
    glm::vec3 emissiveColor = glm::vec3(0.0f); // 默认为黑色(不发光)
    float emissiveStrength = 1.0f;             // 发光强度

    // 透明的贴图设置
    float alphaCutoff = 0.5f; // 低于此值的像素将被丢弃

    MeshShapeType shapeType = MeshShapeType::Cube;
    MeshParams params;

    MeshComponent(std::shared_ptr<Model> m, bool gizmo = false);

    ComponentType getType() const override { return Type; }

    void setMesh(std::shared_ptr<Model> newModel);
};

// ==========================================
// 3. 光照组件
// ==========================================


class LightComponent : public Component
{
public:
    static constexpr ComponentType Type = ComponentType::Light;

    LightType type;
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;

    // 阴影开关
    // 默认为 true，方便起见，但通常只有 Directional 会用到
    bool castShadows = true;

    // 衰减
    // float constant = 1.0f;
    // float linear = 0.09f;
    // float quadratic = 0.032f;
    float range = 10.0f; // 光照有效半径 (米)

    // 聚光
    float cutOff = glm::cos(glm::radians(12.5f));
    float outerCutOff = glm::cos(glm::radians(17.5f));

    // 阴影投射时的剔除面
    // GL_BACK (0x0405): 剔除背面 (默认，适合闭合物体，解决漏光)
    // GL_FRONT (0x0404): 剔除正面 (适合解决 Shadow Acne，Unity/UE常用技巧)
    unsigned int shadowCullFace = GL_BACK;

    // Unity 风格的阴影参数
    // Depth Bias: 对应 Unity 的 "Bias"，通常很小 (0.001 ~ 0.05)
    float shadowBias = 0.0010f; 
    
    // Normal Bias: 对应 Unity 的 "Normal Bias"，收缩模型的程度 (0.0 ~ 3.0)
    // 我们的单位是世界坐标单位，所以默认值设小一点，比如 0.02
    float shadowNormalBias = 0.00f;

    // 阴影艺术控制
    float shadowStrength = 1.0f; // 0=无阴影, 1=全黑
    float shadowRadius = 0.05f;  // 控制 PCF 采样范围 (软阴影程度)

    LightComponent(LightType t);

    ComponentType getType() const override { return Type; }
};

// ==========================================
// 4. 反射探针组件
// ==========================================
class ReflectionProbeComponent : public Component
{
public:
    static constexpr ComponentType Type = ComponentType::ReflectionProbe;

    int resolution = 2048;
    unsigned int textureID = 0;
    unsigned int fboID = 0;
    unsigned int rboID = 0;
    bool isDirty = true;
    // 影响范围/房间大小 (默认 10x10x10 的房间)
    glm::vec3 boxSize = glm::vec3(10.0f, 10.0f, 10.0f);

    ReflectionProbeComponent() = default;
    ~ReflectionProbeComponent(); // 析构移到 cpp (因为它包含 glDelete)

    void initGL(); // 核心逻辑移到 cpp
    ComponentType getType() const override { return Type; }
};

// ==========================================
// 5. 游戏对象
// ==========================================
class GameObject
{
public:
    std::string name;
    Transform transform;
    std::vector<std::unique_ptr<Component>> components;

    GameObject(const std::string &n);

    int getInstanceID() const { return _instanceId; }

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
            if (comp->getType() == T::Type)
            {
                // 安全的向下转型 (因为我们已经确认了类型)
                return static_cast<T *>(comp.get());
            }
        }
        return nullptr;
    }

    void removeComponent(Component *comp);

private:
    int _instanceId;
};
