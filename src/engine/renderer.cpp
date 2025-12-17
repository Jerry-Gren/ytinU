#include "renderer.h"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

Renderer::Renderer() {
    // 构造函数可以留空，把初始化放在 init() 里更安全
}

Renderer::~Renderer() {
    // unique_ptr 和 shared_ptr 会自动释放资源
}

void Renderer::init() {
    // 1. 初始化 Shader (从 SceneRoaming::initShader 搬运过来)
    // 请将 vsCode, fsCode, gridVs, gridFs, skyVs, skyFs 的定义和 link 逻辑放在这里
    // 例如：
    // _mainShader.reset(new GLSLProgram);
    // _mainShader->attachVertexShader(vsCode); ...
    
    // [注意] 这里你需要把 SceneRoaming.cpp 里那一大段 const char* shader code 复制过来
    // ... (省略几百行 Shader 代码) ...
    // 调用 initShader(); 
    // 顶点着色器 (Vertex Shader)
    const char *vsCode = R"(
        #version 330 core
        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec3 aNormal;
        layout(location = 2) in vec2 aTexCoord;

        out vec3 FragPos;
        out vec3 Normal;
        out vec2 TexCoord;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        void main() {
            FragPos = vec3(model * vec4(aPosition, 1.0));
            // 使用 Normal Matrix 修正法线 (防止非均匀缩放导致法线错误)
            Normal = mat3(transpose(inverse(model))) * aNormal;
            TexCoord = aTexCoord;
            
            gl_Position = projection * view * vec4(FragPos, 1.0);
        }
    )";

    // 片元着色器 (Fragment Shader) - Blinn-Phong 多光源版本
    const char *fsCode = R"(
        #version 330 core
        out vec4 FragColor;

        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;

        // 材质定义
        struct Material {
            vec3 ambient;
            vec3 diffuse;
            vec3 specular;
            float shininess;
        }; 

        // 平行光定义
        struct DirLight {
            vec3 direction;
            vec3 color;
            float intensity;
        };

        // 点光源定义
        struct PointLight {
            vec3 position;
            float constant;
            float linear;
            float quadratic;
            vec3 color;
            float intensity;
        };

        // 聚光灯定义
        struct SpotLight {
            vec3 position;
            vec3 direction;
            float cutOff;
            float outerCutOff;
            float constant;
            float linear;
            float quadratic;
            vec3 color;
            float intensity;
        };

        // 定义最大光源数量常量
        #define NR_POINT_LIGHTS 4
        #define NR_SPOT_LIGHTS 4

        uniform bool isUnlit;
        uniform bool isDoubleSided;
        uniform bool fixDoubleSide;

        uniform vec3 viewPos;
        uniform Material material;
        
        // 我们允许多个平行光(如多个太阳)
        #define NR_DIR_LIGHTS 2 
        uniform DirLight dirLights[NR_DIR_LIGHTS];
        uniform int dirLightCount; // 实际传入的数量

        uniform PointLight pointLights[NR_POINT_LIGHTS];
        uniform int pointLightCount;

        uniform SpotLight spotLights[NR_SPOT_LIGHTS];
        uniform int spotLightCount;

        // 函数声明
        vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir);
        vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir);
        vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir);

		// 获取法线辅助函数
		vec3 getNormal() {
            vec3 n = normalize(Normal);
            
            // 获取当前的面向状态
            bool isFront = gl_FrontFacing;
            
            // [macOS 补丁]
            // 如果开启修复，我们强制反转“由于某种原因被判定为背面但实际上可见”的面
            if (fixDoubleSide) {
                isFront = !isFront;
            }

            // 只有当开启双面，且判定为背面时，才反转法线
            if (isDoubleSided && !isFront) {
                n = -n; 
            }
            return n;
        }
        
        void main() {
            if (isUnlit) {
                FragColor = vec4(material.diffuse, 1.0); 
                return;
            }
            
            vec3 norm = getNormal();
            vec3 viewDir = normalize(viewPos - FragPos);
            
            vec3 result = vec3(0.0);

            for(int i = 0; i < dirLightCount; i++)
                result += CalcDirLight(dirLights[i], norm, viewDir);
            
            for(int i = 0; i < pointLightCount; i++)
                result += CalcPointLight(pointLights[i], norm, FragPos, viewDir);
                
            for(int i = 0; i < spotLightCount; i++)
                result += CalcSpotLight(spotLights[i], norm, FragPos, viewDir);

            FragColor = vec4(result, 1.0);
        }

        // --- 函数实现 ---

        vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir) {
            vec3 lightDir = normalize(-light.direction);
            // 漫反射
            float diff = max(dot(normal, lightDir), 0.0);
            // 镜面反射 (Blinn-Phong)
            vec3 halfwayDir = normalize(lightDir + viewDir);
            float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
            // 合并
            vec3 ambient = light.color * light.intensity * material.ambient;
            vec3 diffuse = light.color * light.intensity * diff * material.diffuse;
            vec3 specular = light.color * light.intensity * spec * material.specular;
            return (ambient + diffuse + specular);
        }

        vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir) {
            vec3 lightDir = normalize(light.position - fragPos);
            // 衰减
            float distance = length(light.position - fragPos);
            float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    
            // 漫反射
            float diff = max(dot(normal, lightDir), 0.0);
            // 镜面反射
            vec3 halfwayDir = normalize(lightDir + viewDir);
            float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
            // 合并
            vec3 ambient = light.color * light.intensity * material.ambient * attenuation;
            vec3 diffuse = light.color * light.intensity * diff * material.diffuse * attenuation;
            vec3 specular = light.color * light.intensity * spec * material.specular * attenuation;
            return (ambient + diffuse + specular);
        }

        vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir) {
            vec3 lightDir = normalize(light.position - fragPos);
            // 衰减
            float distance = length(light.position - fragPos);
            float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    
            // 聚光强度 (Soft edges)
            float theta = dot(lightDir, normalize(-light.direction)); 
            float epsilon = light.cutOff - light.outerCutOff;
            float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
            
            // 漫反射
            float diff = max(dot(normal, lightDir), 0.0);
            // 镜面反射
            vec3 halfwayDir = normalize(lightDir + viewDir);
            float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
            // 合并
            vec3 ambient = light.color * light.intensity * material.ambient * attenuation; // 通常聚光灯环境光很弱或没有，这里加上防止全黑
            vec3 diffuse = light.color * light.intensity * diff * material.diffuse * attenuation * intensity;
            vec3 specular = light.color * light.intensity * spec * material.specular * attenuation * intensity;
            return (ambient + diffuse + specular);
        }
    )";

    _mainShader.reset(new GLSLProgram);
    _mainShader->attachVertexShader(vsCode);
    _mainShader->attachFragmentShader(fsCode);
    _mainShader->link();

    // =============================================================
    // 1. 无限网格 Shader (Unity 风格)
    // =============================================================
    const char* gridVs = R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        uniform mat4 view;
        uniform mat4 projection;
        uniform vec3 viewPos;
        
        out vec3 WorldPos;
        out float Near;
        out float Far;

        void main() {
            // 我们把一个小的平面放大很多倍来模拟无限
            vec3 pos = aPos * 1000.0; // 放大平面
            pos.y = 0.0; // 强制在 XZ 平面
            WorldPos = pos;
            gl_Position = projection * view * vec4(pos, 1.0);
            
            // 传递裁剪面信息用于淡出计算
            Near = 0.1; 
            Far = 100.0; 
        }
    )";

    const char* gridFs = R"(
        #version 330 core
        out vec4 FragColor;
        in vec3 WorldPos;
        in float Near;
        in float Far;

        uniform vec3 viewPos;

        void main() {
            vec2 coord = WorldPos.xz;
            vec2 derivative = fwidth(coord);
            
            // --- 基础参数 ---
            // 亮白色线条，在深色背景下更清晰
            vec3 gridColor = vec3(0.7, 0.7, 0.7); 
            
            // 1. 绘制小格子 (1米)
            vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
            float line = min(grid.x, grid.y);
            float minimumz = min(derivative.y, 1.0);
            float minimumx = min(derivative.x, 1.0);
            
            // 小格子透明度低一点 (0.3)
            vec4 color = vec4(gridColor, 0.3 * (1.0 - min(line, 1.0))); 

            // 2. 绘制大格子 (10米)
            vec2 coord2 = coord / 10.0;
            vec2 derivative2 = fwidth(coord2);
            vec2 grid2 = abs(fract(coord2 - 0.5) - 0.5) / derivative2;
            float line2 = min(grid2.x, grid2.y);
            
            // 如果是大格子线，透明度高一点 (0.8)，覆盖小格子
            if(1.0 - min(line2, 1.0) > 0.1) {
                color = vec4(gridColor, 0.8 * (1.0 - min(line2, 1.0)));
            }

            // 3. 轴线高亮 (X轴红色，Z轴蓝色) - 类似 Unity 编辑器
            // 当 z 接近 0 时是 X 轴
            // if(abs(WorldPos.z) < 0.05) color = vec4(1.0, 0.2, 0.2, 1.0); // Red X-Axis
            // 当 x 接近 0 时是 Z 轴
            // if(abs(WorldPos.x) < 0.05) color = vec4(0.2, 0.2, 1.0, 1.0); // Blue Z-Axis

            // 4. 距离淡出 (Fade out)
            float dist = distance(viewPos.xz, WorldPos.xz);
            float alpha = 1.0 - smoothstep(10.0, 400.0, dist);
            color.a *= alpha;

            if (color.a <= 0.0) discard;
            FragColor = color;
        }
    )";

    _gridShader.reset(new GLSLProgram);
    _gridShader->attachVertexShader(gridVs);
    _gridShader->attachFragmentShader(gridFs);
    _gridShader->link();

    // =============================================================
    // 2. 程序化天空盒 Shader (Unity 默认风格)
    // =============================================================
    const char* skyVs = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        out vec3 TexCoords;
        uniform mat4 projection;
        uniform mat4 view;

        void main() {
            TexCoords = aPos;
            // 移除平移分量，让天空盒永远跟着相机
            vec4 pos = projection * mat4(mat3(view)) * vec4(aPos, 1.0);
            
            // [技巧] 让天空盒永远在深度测试的最远处 (z = w, 透视除法后 z/w = 1.0)
            gl_Position = pos.xyww; 
        }
    )";

    const char* skyFs = R"(
        #version 330 core
        out vec4 FragColor;
        in vec3 TexCoords;

        void main() {
            vec3 dir = normalize(TexCoords);
            float y = dir.y;

            // =========================================================
            // [配色方案] 
            // =========================================================
            
            // 1. 地面颜色 (Deep Dark Gray)
            vec3 colGround = vec3(0.2, 0.2, 0.2); 

            // 2. 地平线颜色 (Horizon Fog)
            // [修改]: 稍微提亮一点，增加一点“厚重感”和不透明度
            vec3 colHorizon = vec3(0.7, 0.75, 0.82); 

            // 3. 天顶颜色 (Sky Zenith)
            vec3 colZenith  = vec3(0.2, 0.45, 0.8); 

            vec3 finalColor;

            // =========================================================
            // [混合逻辑] 
            // =========================================================
            
            if (y < 0.0) {
                // --- 地下部分 ---
                
                // [核心改进 1: 平滑过渡]
                // 原代码这里使用了 colHorizon * 0.5，导致和上半部分产生接缝。
                // 我们现在直接从 colHorizon 开始，确保 y=0 处无缝连接。

                // [核心改进 2: 加大雾气密度]
                // 我们不改变 -0.2 这个范围，而是改变混合曲线的"形状"。
                // 原始线性混合会让地面黑得太快。
                // 这里先算出线性因子 factorLinear (0.0 到 1.0)
                float factorLinear = smoothstep(0.0, -0.2, y);
                
                // 使用 pow 函数处理因子。
                // 0.4 的指数会让混合因子在接近 0 (地平线) 的地方停留更久，
                // 从而让雾气颜色"渗"入地面更多，看起来雾更浓，但并没有扩大实际渲染范围。
                float factorCurved = pow(factorLinear, 0.4); 

                finalColor = mix(colHorizon, colGround, factorCurved); 
            } 
            else {
                // --- 天空部分 ---
                
                // 同样为了增加雾气感，我们让天顶蓝色的出现稍微"迟"一点
                // 0.5 的指数比原来的 0.7 更小，意味着白色雾气会向上延伸得更有力
                float t = pow(y, 0.5); 
                finalColor = mix(colHorizon, colZenith, t);
            }

            // [色调映射] (可选) 
            // 加上轻微的 Gamma 矫正或 Tone Mapping 可以让雾气看起来更柔和
            // finalColor = pow(finalColor, vec3(1.0/2.2)); 

            FragColor = vec4(finalColor, 1.0);
        }
    )";

    _skyboxShader.reset(new GLSLProgram);
    _skyboxShader->attachVertexShader(skyVs);
    _skyboxShader->attachFragmentShader(skyFs);
    _skyboxShader->link();

    // 2. 初始化模型资源
    _gridPlane = GeometryFactory::createPlane(2.0f, 2.0f);
    _skyboxCube = GeometryFactory::createCube(1.0f);

    // 3. 初始化 OutlinePass (初始大小可以给 0 或窗口大小，后面 onResize 会修)
    _outlinePass = std::make_unique<OutlinePass>(1920, 1080);
}

void Renderer::onResize(int width, int height) {
    if (_outlinePass) {
        _outlinePass->onResize(width, height);
    }
}

void Renderer::render(const Scene& scene, Camera* camera, 
                      GLuint targetFBO, int width, int height, 
                      float contentScale,
                      GameObject* selectedObj)
{
    // 1. 绑定目标 FBO
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glViewport(0, 0, width, height);

    // 2. 清屏
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // 3. 准备矩阵
    glm::mat4 view = camera->getViewMatrix();
    glm::mat4 proj = camera->getProjectionMatrix();
    glm::vec3 viewPos = camera->transform.position;

    // --- Pass 0: 天空盒 ---
    drawSkybox(view, proj);

    // --- Pass 1 & 2: 物体渲染 (光照 + 绘制) ---
    drawSceneObjects(scene, view, proj, viewPos);

    // --- Pass 2.5: 网格 ---
    drawGrid(view, proj, viewPos);

    // --- Pass 3: 描边 ---
    if (selectedObj) {
        // OutlinePass 需要传入宽高用于重新生成纹理
        _outlinePass->render(selectedObj, camera, contentScale, width, height);
        
        // 恢复 FBO 绑定 (防止 OutlinePass 内部解绑)
        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    }

    // 解绑
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// === 下面是将 SceneRoaming::renderScene 中的逻辑拆分出来的私有函数 ===

void Renderer::drawSkybox(const glm::mat4& view, const glm::mat4& proj) {
    glDepthFunc(GL_LEQUAL);
    _skyboxShader->use();
    _skyboxShader->setUniformMat4("view", view);
    _skyboxShader->setUniformMat4("projection", proj);
    
    glDisable(GL_CULL_FACE);
    _skyboxCube->draw();
    glEnable(GL_CULL_FACE);
    
    glDepthFunc(GL_LESS);
}

void Renderer::drawGrid(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& viewPos) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glDisable(GL_CULL_FACE);

    _gridShader->use();
    _gridShader->setUniformMat4("view", view);
    _gridShader->setUniformMat4("projection", proj);
    _gridShader->setUniformVec3("viewPos", viewPos);
    
    _gridPlane->draw();

    glEnable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::drawSceneObjects(const Scene& scene, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& viewPos) {
    _mainShader->use();
    _mainShader->setUniformMat4("projection", proj);
    _mainShader->setUniformMat4("view", view);
    _mainShader->setUniformVec3("viewPos", viewPos);

    // Fix for macOS
#ifdef __APPLE__
    _mainShader->setUniformBool("fixDoubleSide", true);
#else
    _mainShader->setUniformBool("fixDoubleSide", false);
#endif

    glFrontFace(GL_CCW);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // 1. 收集光源 (Lighting Loop)
    int dirCount = 0, pointCount = 0, spotCount = 0;
    
    for (const auto& go : scene.getGameObjects()) {
        auto light = go->getComponent<LightComponent>();
        if (light && light->enabled) {
            // ... (复制 SceneRoaming 中的光源收集逻辑，注意变量名 go->transform) ...
            // 注意：这里需要原封不动地把那段长长的 if/else light type 判断拷过来
            // 并调用 _mainShader->setUniform...
            std::string baseName;
            if (light->type == LightType::Directional && dirCount < 2)
            {
                baseName = "dirLights[" + std::to_string(dirCount++) + "]";
                glm::vec3 dir = go->transform.rotation * glm::vec3(0, 0, -1);
                _mainShader->setUniformVec3(baseName + ".direction", dir);
            }
            else if (light->type == LightType::Point && pointCount < 4)
            {
                baseName = "pointLights[" + std::to_string(pointCount++) + "]";
                _mainShader->setUniformVec3(baseName + ".position", go->transform.position);
                _mainShader->setUniformFloat(baseName + ".constant", light->constant);
                _mainShader->setUniformFloat(baseName + ".linear", light->linear);
                _mainShader->setUniformFloat(baseName + ".quadratic", light->quadratic);

                // 同步 Gizmo 颜色
                if (auto mesh = go->getComponent<MeshComponent>()) {
                    if (mesh->isGizmo) mesh->material.diffuse = light->color;
                }
            }
            else if (light->type == LightType::Spot && spotCount < 4)
            {
                baseName = "spotLights[" + std::to_string(spotCount++) + "]";
                _mainShader->setUniformVec3(baseName + ".position", go->transform.position);
                glm::vec3 dir = go->transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
                _mainShader->setUniformVec3(baseName + ".direction", dir);
                _mainShader->setUniformFloat(baseName + ".cutOff", light->cutOff);
                _mainShader->setUniformFloat(baseName + ".outerCutOff", light->outerCutOff);
                _mainShader->setUniformFloat(baseName + ".constant", light->constant);
                _mainShader->setUniformFloat(baseName + ".linear", light->linear);
                _mainShader->setUniformFloat(baseName + ".quadratic", light->quadratic);

                if (auto mesh = go->getComponent<MeshComponent>()) {
                    if (mesh->isGizmo) mesh->material.diffuse = light->color;
                }
            }

            if (!baseName.empty())
            {
                _mainShader->setUniformVec3(baseName + ".color", light->color);
                _mainShader->setUniformFloat(baseName + ".intensity", light->intensity);
            }
        }
    }
    _mainShader->setUniformInt("dirLightCount", dirCount);
    _mainShader->setUniformInt("pointLightCount", pointCount);
    _mainShader->setUniformInt("spotLightCount", spotCount);

    // 2. 绘制物体 (Mesh Loop)
    for (const auto& go : scene.getGameObjects()) {
        auto meshComp = go->getComponent<MeshComponent>();
        if (!meshComp || !meshComp->enabled) continue;
        
        auto lightComp = go->getComponent<LightComponent>();
        
        // 双面渲染处理
        if (meshComp->doubleSided) glDisable(GL_CULL_FACE);

        // ... (复制 SceneRoaming 中的材质设置和 ModelMatrix 计算逻辑) ...
        // 材质与光照同步逻辑
        // if (lightComp) {
        //     meshComp->material.diffuse = lightComp->color;
        //     meshComp->material.ambient = lightComp->color * 0.1f;
        // }
        
        _mainShader->setUniformBool("isUnlit", meshComp->isGizmo);
        _mainShader->setUniformBool("isDoubleSided", meshComp->doubleSided);
        _mainShader->setUniformVec3("material.ambient", meshComp->material.ambient);
        _mainShader->setUniformVec3("material.diffuse", meshComp->material.diffuse);
        _mainShader->setUniformVec3("material.specular", meshComp->material.specular);
        _mainShader->setUniformFloat("material.shininess", meshComp->material.shininess);

        // 计算 Model Matrix
        glm::mat4 modelMatrix = go->transform.getLocalMatrix();
        modelMatrix = modelMatrix * meshComp->model->transform.getLocalMatrix();

        _mainShader->setUniformMat4("model", modelMatrix);
        
        meshComp->model->draw();

        if (meshComp->doubleSided) glEnable(GL_CULL_FACE);
    }
}