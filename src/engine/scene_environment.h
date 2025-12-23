#pragma once
#include <glm/glm.hpp>
#include <string>

enum class SkyboxType {
    Procedural, // 程序化渐变
    CubeMap     // HDR 贴图
};

struct SceneEnvironment {
    // --- 通用设置 ---
    SkyboxType type = SkyboxType::Procedural;
    float globalExposure = 1.0f; // 全局曝光控制

    // --- 程序化天空参数 ---
    glm::vec3 skyZenithColor = glm::vec3(0.2f, 0.45f, 0.8f);   // 天顶蓝
    glm::vec3 skyHorizonColor = glm::vec3(0.7f, 0.75f, 0.82f); // 地平线灰白
    glm::vec3 groundColor = glm::vec3(0.2f, 0.2f, 0.2f);       // 地面深灰
    float skyEnergy = 1.0f; // 亮度倍率

    // --- HDR 贴图参数 (阶段二用) ---
    std::string hdrFilePath;
};