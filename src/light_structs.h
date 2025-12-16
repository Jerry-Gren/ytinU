#pragma once
#include <glm/glm.hpp>

// 材质属性
struct Material
{
    glm::vec3 ambient = glm::vec3(0.1f);
    glm::vec3 diffuse = glm::vec3(0.7f);
    glm::vec3 specular = glm::vec3(0.5f);
    float shininess = 32.0f;
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