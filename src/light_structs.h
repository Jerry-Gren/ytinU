#pragma once
#include <glm/glm.hpp>

// 材质属性
struct Material
{
    // --- PBR 核心参数 ---
    glm::vec3 albedo = glm::vec3(1.0f); // 基础色 (原 diffuse)
    float metallic = 0.0f;              // 金属度 (0=绝缘体, 1=金属)
    float roughness = 0.5f;             // 粗糙度 (0=光滑, 1=粗糙)
    float ao = 1.0f;                    // 环境光遮蔽 (Ambient Occlusion)

    // --- 透明/玻璃高级参数 ---
    // PBR 工作流中，F0 (反射率) 通常由 metallic 决定
    // 但为了兼容你的玻璃效果，我们保留 IOR 和 transparency
    float refractionIndex = 1.52f; // 折射率 (玻璃默认 1.52)
    float transparency = 0.0f;     // 透明度 (0=不透明)
    
    // [兼容性保留] 
    // 反射率 (Reflectivity) 在 PBR 中通常不需要手动调 (非金属固定0.04，金属等于Albedo)
    // 但为了让现在的 Shader 能跑，或者作为额外的艺术控制，我们暂时保留它
    float reflectivity = 0.5f;
};

// 平行光 (太阳光)
struct DirLight
{
    glm::vec3 direction = glm::vec3(-0.2f, -1.0f, -0.3f);
    glm::vec3 color = glm::vec3(1.0f); // 包含强度
    float intensity = 1.0f;
};

// 点光源 (灯泡)
struct PointLight
{
    glm::vec3 position = glm::vec3(0.0f);

    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;

    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
};

// 聚光灯 (手电筒)
struct SpotLight
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    float cutOff = glm::cos(glm::radians(12.5f));
    float outerCutOff = glm::cos(glm::radians(17.5f));

    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;

    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
};