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
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
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

        out vec3 LocalPos;
        // 输出视空间深度 (或者直接用 gl_Position.w)
        out float ClipSpaceZ;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        void main() {
            vec4 worldPos = model * vec4(aPosition, 1.0);
            FragPos = vec3(worldPos);
            
            // 使用 Normal Matrix 修正法线 (防止非均匀缩放导致法线错误)
            Normal = mat3(transpose(inverse(model))) * aNormal;
            TexCoord = aTexCoord;

            LocalPos = aPosition;
            
            gl_Position = projection * view * worldPos;
            
            // 保存 View Space 的深度 (对于透视投影，w 分量就是 -ViewZ)
            // 用于 CSM 层级选择
            ClipSpaceZ = gl_Position.w;
        }
    )";

    // 片元着色器 (Fragment Shader) - Blinn-Phong 多光源版本
    const char *fsCode = R"(
        #version 330 core
        out vec4 FragColor;

        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;
        in vec3 LocalPos;
        in float ClipSpaceZ;

        // 材质定义
        struct Material {
            vec3 ambient;
            vec3 diffuse;
            vec3 specular;
            float shininess;

            float reflectivity;
            float refractionIndex;
            float transparency;
        }; 

        uniform sampler2D diffuseMap; 
        uniform bool hasDiffuseMap;
        uniform bool useTriplanar;
        uniform float triplanarScale;

        // 动态环境贴图
        uniform samplerCube envMap;
        uniform bool hasEnvMap;

        // 视差校正所需的 Uniforms
        uniform vec3 probePos;    // 探针拍摄时的中心位置 (世界坐标)
        uniform vec3 probeBoxMin; // 房间的最小边界 (世界坐标)
        uniform vec3 probeBoxMax; // 房间的最大边界 (世界坐标)

        // 平行光定义
        struct DirLight {
            vec3 direction;
            vec3 color;
            float intensity;
            int shadowIndex; // -1 = 无阴影, >=0 = 纹理数组起始层级
        };

        // 点光源定义
        struct PointLight {
            vec3 position;
            float range;
            vec3 color;
            float intensity;
            int shadowIndex; // -1 = 无阴影, >=0 = 对应 pointShadowMaps 的下标
            float shadowStrength; // 阴影深浅
            float shadowRadius;   // 阴影软硬
            float shadowBias;
        };

        // 聚光灯定义
        struct SpotLight {
            vec3 position;
            vec3 direction;
            float cutOff;
            float outerCutOff;
            float range;
            vec3 color;
            float intensity;
        };

        // 定义最大光源数量常量
        #define NR_DIR_LIGHTS 4
        #define NR_POINT_LIGHTS 4
        #define NR_SPOT_LIGHTS 4
        
        // 最大支持的点光源阴影数
        #define NR_POINT_SHADOWS 4

        uniform bool isUnlit;
        uniform bool isDoubleSided;
        uniform bool isDebug;

        uniform vec3 viewPos;
        uniform Material material;
        
        uniform DirLight dirLights[NR_DIR_LIGHTS];
        uniform int dirLightCount;

        uniform PointLight pointLights[NR_POINT_LIGHTS];
        uniform int pointLightCount;

        uniform SpotLight spotLights[NR_SPOT_LIGHTS];
        uniform int spotLightCount;

        // --- CSM (平行光) 阴影 Uniforms ---
        uniform sampler2DArrayShadow shadowMap; 
        // 假设最大 4 个灯 * 6 层级联 = 24 个矩阵
        // 为安全起见定义 32
        uniform mat4 lightSpaceMatrices[32];
        uniform float cascadePlaneDistances[16];
        uniform int cascadeCount;
        uniform float shadowBias;

        // --- 点光源阴影 Uniforms ---
        uniform samplerCube pointShadowMaps[NR_POINT_SHADOWS];
        uniform float pointShadowFarPlanes[NR_POINT_SHADOWS];

        // 函数声明
        vec4 getTriplanarSample(vec3 worldPos, vec3 normal);

        vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, vec3 albedo, float shadow);
        vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 albedo, float shadow);
        vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 albedo);

        vec3 BoxProjectedCubemapDirection(vec3 worldPos, vec3 worldRefDir, vec3 pPos, vec3 boxMin, vec3 boxMax);

        float ShadowCalculation(vec3 fragPosWorld, vec3 normal, vec3 lightDir, float viewSpaceDepth, int baseLayerIndex);
        float CalcPointShadow(vec3 fragPos, vec3 lightPos, int shadowIndex, float range, float radius, float bias);

        float GetAttenuation(float distance, float range);

		// 获取法线辅助函数
		vec3 getNormal() {
            // 归一化插值后的法线
            vec3 n = normalize(Normal);
            // 只有当物体明确开启了双面渲染(isDoubleSided == true)，
            // 并且我们正在渲染背面(!gl_FrontFacing)时，才反转法线。
            // 对于普通的球体/立方体，这段逻辑将被跳过，从而避免了 macOS 上的误判问题。
            if (isDoubleSided && !gl_FrontFacing) {
                n = -n; 
            }
            return n;
        }
        
        void main() {
            vec3 norm = getNormal();
            vec3 baseDiffuse = material.diffuse;

            // 基础纹理采样
            if (hasDiffuseMap) {
                vec4 texColor;
                // 根据开关选择采样方式
                if (useTriplanar) {
                    texColor = getTriplanarSample(LocalPos, norm);
                } else {
                    texColor = texture(diffuseMap, TexCoord);
                }
                baseDiffuse = texColor.rgb * material.diffuse; 
            }

            // Unlit 模式直接返回
            if (isUnlit) {
                FragColor = vec4(baseDiffuse, 1.0); 
                return;
            }
            
            vec3 viewDir = normalize(viewPos - FragPos);
            
            // 计算标准 Phong 光照
            // 先计算全局基础环境光
            // 我们可以取一个固定的环境光颜色，或者取第一个平行光的颜色作为环境基调
            // 这里为了简单，我们假设环境光是白色的微弱光 (0.02 强度) * 材质的环境光系数
            vec3 ambient = vec3(0.02) * material.ambient;
            
            // 如果有平行光，我们可以用第一个平行光的颜色来影响环境光，稍微自然一点（可选）
            if (dirLightCount > 0) {
                ambient = dirLights[0].color * 0.05 * material.ambient; 
            }

            // 初始化 result 为环境光
            vec3 result = ambient;

            // 循环计算所有平行光
            for(int i = 0; i < dirLightCount; i++) {
                float shadow = 1.0;
                
                // 如果该光源启用了阴影 (index >= 0)
                if (dirLights[i].shadowIndex >= 0) {
                    vec3 lightDir = normalize(-dirLights[i].direction);
                    // 传入该光源在纹理数组中的起始层级
                    shadow = ShadowCalculation(FragPos, norm, lightDir, ClipSpaceZ, dirLights[i].shadowIndex);
                }
                
                // 传入 shadow 因子
                result += CalcDirLight(dirLights[i], norm, viewDir, baseDiffuse, shadow); 
            }
            
            // 点光源计算
            for(int i = 0; i < pointLightCount; i++) {
                float shadow = 1.0;
                if (pointLights[i].shadowIndex >= 0) {
                    float rawShadow = CalcPointShadow(FragPos, pointLights[i].position, pointLights[i].shadowIndex, pointLights[i].range, pointLights[i].shadowRadius, pointLights[i].shadowBias);
                    
                    // 应用 Shadow Strength (混合: 1.0 = 纯影, 0.0 = 无影)
                    // 如果 shadow 是 0 (全黑), strength 是 0.8, 结果应为 0.2
                    // 公式: 最终阴影因子 = mix(1.0, rawShadow, strength);
                    shadow = mix(1.0, rawShadow, pointLights[i].shadowStrength);
                }
                result += CalcPointLight(pointLights[i], norm, FragPos, viewDir, baseDiffuse, shadow);
            }
                
            for(int i = 0; i < spotLightCount; i++)
                result += CalcSpotLight(spotLights[i], norm, FragPos, viewDir, baseDiffuse);

            // 3. 环境反射与折射
            if (hasEnvMap) {
                vec3 I = normalize(FragPos - viewPos); // 视线向量 I
                vec3 N = norm;                         // 法线 N

                // ------------------------------------------
                // 1. 计算菲涅尔系数 (Fresnel)
                // ------------------------------------------
                // 描述：视线与法线越垂直(边缘)，反射越强(F接近1)；越平行(中心)，折射越强(F接近0)
                // F0 是基础反射率：
                //   - 水/玻璃等非金属 (Dielectric) 约为 0.04
                //   - 金属 (Metal) 约为 material.diffuse 颜色本身
                // 我们用 reflectivity 参数来控制这个 F0
                float F0_val = mix(0.04, 1.0, material.reflectivity);
                vec3 F0 = vec3(F0_val);
                
                // Schlick 近似公式
                float cosTheta = clamp(dot(N, -I), 0.0, 1.0);
                vec3 F = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);

                // ------------------------------------------
                // 2. 计算反射 (Reflection)
                // ------------------------------------------
                vec3 reflectColor = vec3(0.0);
                {
                    vec3 R = reflect(I, N);
                    // 应用视差校正
                    vec3 correctedR = BoxProjectedCubemapDirection(FragPos, R, probePos, probeBoxMin, probeBoxMax);
                    reflectColor = texture(envMap, correctedR).rgb;
                }

                // ------------------------------------------
                // 3. 计算折射 + 色散 (Refraction + Dispersion)
                // ------------------------------------------
                vec3 refractColor = vec3(0.0);
                if (material.transparency > 0.01) {
                    // 基础折射率比
                    float k = (material.refractionIndex < 1.0) ? 1.0 : material.refractionIndex;
                    float ratio = 1.00 / k;
                    
                    // [色散核心]：为 R, G, B 通道使用微小差异的折射率
                    // 0.01 ~ 0.02 的偏移量通常能产生很好的钻石/厚玻璃效果
                    float dispersion = 0.02; 
                    
                    vec3 R_r = refract(I, N, ratio * (1.0 - dispersion)); // 红光折射少
                    vec3 R_g = refract(I, N, ratio);                      // 绿光居中
                    vec3 R_b = refract(I, N, ratio * (1.0 + dispersion)); // 蓝光折射多

                    // 分别对 R, G, B 进行视差校正采样
                    vec3 correctedR_r = BoxProjectedCubemapDirection(FragPos, R_r, probePos, probeBoxMin, probeBoxMax);
                    vec3 correctedR_g = BoxProjectedCubemapDirection(FragPos, R_g, probePos, probeBoxMin, probeBoxMax);
                    vec3 correctedR_b = BoxProjectedCubemapDirection(FragPos, R_b, probePos, probeBoxMin, probeBoxMax);

                    // 分别采样并组合
                    float r = texture(envMap, correctedR_r).r;
                    float g = texture(envMap, correctedR_g).g;
                    float b = texture(envMap, correctedR_b).b;
                    
                    refractColor = vec3(r, g, b);
                }

                // ------------------------------------------
                // 4. 最终物理混合 (Mix based on Fresnel)
                // ------------------------------------------
                // 如果物体是透明的 (transparency > 0)
                // 最终颜色 = 反射 * F + 折射 * (1 - F)
                // 这种混合方式保证了能量守恒：光线要么反射走，要么折射进去
                
                vec3 glassColor = mix(refractColor, reflectColor, F);
                
                // 最后，我们要把这个“玻璃计算结果”和物体原本的颜色(Phong光照)混合
                // 如果 transparency = 1.0 (全透明)，我们只显示 glassColor
                // 如果 transparency = 0.0 (不透明)，我们主要显示 result (Phong光照) + 表面反射
                
                if (material.transparency > 0.01) {
                    // 全透明模式：忽略漫反射本身，只保留高光
                    // (玻璃本身几乎没有漫反射，只有镜面反射和折射)
                    // 我们保留一点 result 里的 specular 高光
                    result = glassColor + (result * 0.1); // 这里的 0.1 是为了防止全黑，保留一点环境光感
                } else {
                    // 不透明模式 (金属)：简单的反射叠加
                    // 金属也是由 Fresnel 控制的
                    result = mix(result, reflectColor, material.reflectivity * F); 
                }
            }

            FragColor = vec4(result, 1.0);

            // Debug Cascade Layers (仅显示第一个开启阴影的光源的层级)
            if (isDebug) {
                int layer = -1;
                for (int i = 0; i < cascadeCount; ++i) {
                    if (ClipSpaceZ < cascadePlaneDistances[i]) {
                        layer = i;
                        break;
                    }
                }
                if (layer == -1) layer = cascadeCount;
                vec3 debugColor = vec3(0.0);
                if (layer == 0) debugColor = vec3(1.0, 0.0, 0.0);
                else if (layer == 1) debugColor = vec3(0.0, 1.0, 0.0);
                else if (layer == 2) debugColor = vec3(0.0, 0.0, 1.0);
                else if (layer == 3) debugColor = vec3(1.0, 1.0, 0.0);
                else debugColor = vec3(1.0, 0.0, 1.0);
                FragColor = vec4(mix(FragColor.rgb, debugColor, 0.2), 1.0);
            }
        }

        // --- 函数实现 ---

        vec4 getTriplanarSample(vec3 position, vec3 normal) {
            // 1. 计算混合权重
            // 使用幂函数 (pow) 增加对比度，让主轴方向的纹理更清晰，侧面的纹理快速衰减
            // 这里的 4.0 是“锐度”，数值越大，交界处越硬；数值越小，交界处越模糊
            vec3 blending = abs(normal);
            blending = pow(blending, vec3(4.0)); 
            
            // 归一化权重，确保加起来等于 1
            float b = (blending.x + blending.y + blending.z);
            blending /= vec3(b, b, b);

            // 2. [核心修改] 使用传入的 position (将是 LocalPos) 计算 UV
            vec2 uvX = position.zy * triplanarScale;
            vec2 uvY = position.xz * triplanarScale;
            vec2 uvZ = position.xy * triplanarScale;

            // 3. 采样三次
            vec4 colX = texture(diffuseMap, uvX);
            vec4 colY = texture(diffuseMap, uvY);
            vec4 colZ = texture(diffuseMap, uvZ);

            // 4. 混合结果
            return colX * blending.x + colY * blending.y + colZ * blending.z;
        }

        vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, vec3 albedo, float shadow) {
            vec3 lightDir = normalize(-light.direction);
            // 漫反射
            float diff = max(dot(normal, lightDir), 0.0);
            // 镜面反射 (Blinn-Phong)
            vec3 halfwayDir = normalize(lightDir + viewDir);
            float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
            // 合并
            vec3 diffuse = light.color * light.intensity * diff * albedo * shadow;
            vec3 specular = light.color * light.intensity * spec * material.specular * shadow;
            return (diffuse + specular);
        }

        vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 albedo, float shadow) {
            vec3 lightDir = normalize(light.position - fragPos);
            // 衰减
            float distance = length(light.position - fragPos);
            float attenuation = GetAttenuation(distance, light.range);
            // 漫反射
            float diff = max(dot(normal, lightDir), 0.0);
            // 镜面反射
            vec3 halfwayDir = normalize(lightDir + viewDir);
            float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
            // 合并
            vec3 diffuse = light.color * light.intensity * diff * attenuation * albedo * shadow;
            vec3 specular = light.color * light.intensity * spec * material.specular * attenuation * shadow;
            return (diffuse + specular);
        }

        vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 albedo) {
            vec3 lightDir = normalize(light.position - fragPos);
            // 衰减
            float distance = length(light.position - fragPos);
            float attenuation = GetAttenuation(distance, light.range);
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
            vec3 diffuse = light.color * light.intensity * diff * attenuation * intensity * albedo;
            vec3 specular = light.color * light.intensity * spec * material.specular * attenuation * intensity;
            return (diffuse + specular);
        }

        // Box Projection
        // worldPos: 当前片元的世界坐标
        // worldRefDir: 原始反射向量
        vec3 BoxProjectedCubemapDirection(vec3 worldPos, vec3 worldRefDir, vec3 pPos, vec3 boxMin, vec3 boxMax) {
            vec3 nrdir = normalize(worldRefDir);
            
            // 1. 计算射线与 Box 6个面的交点距离 (类似于 AABB 碰撞检测)
            vec3 rbmax = (boxMax - worldPos) / nrdir;
            vec3 rbmin = (boxMin - worldPos) / nrdir;

            // 2. 找出正向射线的交点 (只关心反射方向那一侧的墙)
            vec3 rbminmax;
            rbminmax.x = (nrdir.x > 0.0) ? rbmax.x : rbmin.x;
            rbminmax.y = (nrdir.y > 0.0) ? rbmax.y : rbmin.y;
            rbminmax.z = (nrdir.z > 0.0) ? rbmax.z : rbmin.z;

            // 3. 取最小的正距离 (最近的交点)
            float fa = min(min(rbminmax.x, rbminmax.y), rbminmax.z);

            // 4. 计算交点位置
            vec3 posonbox = worldPos + nrdir * fa;

            // 5. 将交点转换为相对于探针中心的向量
            return posonbox - pPos;
        }

        // CSM (平行光) 辅助变量
        vec2 poissonDisk[16] = vec2[]( 
            vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ), vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ),
            vec2( -0.91588581, 0.45771432 ), vec2( -0.81544232, -0.87912464 ), vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ),
            vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ), vec2( -0.26496911, -0.41893023 ), vec2( 0.79197514, 0.19090188 ),
            vec2( -0.24188840, 0.99706507 ), vec2( -0.81409955, 0.91437590 ), vec2( 0.19984126, 0.78641367 ), vec2( 0.14383161, -0.14100790 )
        );

        float random(vec3 seed, int i){
            vec4 seed4 = vec4(seed, i);
            float dot_product = dot(seed4, vec4(12.9898,78.233,45.164,94.673));
            return fract(sin(dot_product) * 43758.5453);
        }

        // 平行光阴影计算函数 (PCF + Bias)
        // 返回 0.0 (全阴影) 到 1.0 (无阴影)
        float ShadowCalculation(vec3 fragPosWorld, vec3 normal, vec3 lightDir, float viewSpaceDepth, int baseLayerIndex)
        {
            // 1. 选择级联层级
            int layer = -1;
            for (int i = 0; i < cascadeCount; ++i) {
                if (viewSpaceDepth < cascadePlaneDistances[i]) {
                    layer = i;
                    break;
                }
            }
            if (layer == -1) layer = cascadeCount;
            
            // A. 计算 Z 轴混合权重 (基于视锥分割距离)
            float blendFactor = 0.0;
            int nextLayer = layer + 1;
            if (nextLayer > cascadeCount) nextLayer = cascadeCount;
            
            if (layer < cascadeCount) {
                float splitDist = cascadePlaneDistances[layer];
                float distToEdge = splitDist - viewSpaceDepth;
                float blendBand = 5.0; // Z轴混合带宽度
                
                if (distToEdge < blendBand) {
                    blendFactor = 1.0 - (distToEdge / blendBand);
                }
            }

            // 计算全局矩阵索引 = baseLayerIndex + 局部layer
            int currentMatrixIndex = baseLayerIndex + layer;
            int nextMatrixIndex    = baseLayerIndex + nextLayer;

            // B. 计算 UV 边缘混合权重 (防止侧面漏光/阴影截断)
            // 我们提前计算当前层在光空间的位置，看看是否快出界了
            vec4 fragPosLightSpace = lightSpaceMatrices[currentMatrixIndex] * vec4(fragPosWorld, 1.0);
            vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
            projCoords = projCoords * 0.5 + 0.5;

            // 计算当前点距离纹理中心的距离 (0~0.5)
            vec2 distFromCenter = abs(projCoords.xy - 0.5);
            // 找出最远的那个轴
            float maxDist = max(distFromCenter.x, distFromCenter.y);
            
            // 定义边缘阈值：从 0.4 (80%处) 开始混合，到 0.49 (98%处) 完全切换到下一层
            // 这样保证在出界(0.5)之前，我们已经完全切换到了下一层
            float uvBlendFactor = smoothstep(0.4, 0.49, maxDist);

            // [关键]：最终混合权重取 Z轴 和 UV边缘 的最大值
            // 这意味着：即使 Z 轴认为不需要混合，如果 UV 快出界了，也强制混合！
            blendFactor = max(blendFactor, uvBlendFactor);
            
            // 如果已经是最后一层了，就不能再往后混合了，避免采样越界
            if (layer == cascadeCount) {
                blendFactor = 0.0; 
            }

            // ------------------------------------------------------------------
            // 下面是通用的 PCF 采样逻辑
            // ------------------------------------------------------------------

            // 采样循环
            int layersToSample = (blendFactor > 0.001) ? 2 : 1;
            float layerShadows[2]; 
            layerShadows[0] = 1.0; layerShadows[1] = 1.0; // 默认为1.0(亮)
            
            // PCF 参数
            vec3 N = normalize(normal);
            vec3 L = normalize(lightDir);
            float cosTheta = clamp(dot(N, L), 0.0, 1.0);
            float baseBias = shadowBias * (1.0 - cosTheta);
            baseBias = max(baseBias, shadowBias * 0.1);
            float rotAngle = random(vec3(gl_FragCoord.xy, 1.0), 0) * 6.283185;
            float s = sin(rotAngle); float c = cos(rotAngle);
            mat2 rot = mat2(c, -s, s, c);

            for (int i = 0; i < layersToSample; ++i) 
            {
                int activeLocalLayer = (i == 0) ? layer : nextLayer;
                int activeGlobalIndex = baseLayerIndex + activeLocalLayer;
                
                // 计算当前层级的坐标
                vec4 fPosLight = lightSpaceMatrices[activeGlobalIndex] * vec4(fragPosWorld, 1.0);
                vec3 pCoords = fPosLight.xyz / fPosLight.w;
                pCoords = pCoords * 0.5 + 0.5;

                // 越界检查 (如果出界，直接返回无阴影，交由混合逻辑处理)
                if(pCoords.z > 1.0 || pCoords.x < 0.0 || pCoords.x > 1.0 || pCoords.y < 0.0 || pCoords.y > 1.0) {
                    layerShadows[i] = 1.0; 
                    continue; 
                }

                // 级联 Bias 调整
                float currentBias = baseBias;
                if (activeLocalLayer == 1) currentBias *= 0.5;
                else if (activeLocalLayer == 2) currentBias *= 0.25;
                else if (activeLocalLayer == 3) currentBias *= 0.125;

                float currentDepth = pCoords.z - currentBias;
                
                // 设置 PCF 半径
                float filterRadius = 1.0;
                if (activeLocalLayer == 0) filterRadius = 4.0;
                else if (activeLocalLayer == 1) filterRadius = 2.0;
                else if (activeLocalLayer == 2) filterRadius = 1.0;
                else filterRadius = 0.5;

                vec2 texSize = 1.0 / textureSize(shadowMap, 0).xy;
                
                float shadowSum = 0.0;
                for(int k = 0; k < 16; ++k)
                {
                    vec2 offset = rot * poissonDisk[k];
                    shadowSum += texture(shadowMap, vec4(pCoords.xy + offset * texSize * filterRadius, activeGlobalIndex, currentDepth));
                }
                layerShadows[i] = shadowSum / 16.0; 
            }

            // 最终混合
            float finalVisibility = layerShadows[0];
            if (layersToSample > 1) {
                finalVisibility = mix(layerShadows[0], layerShadows[1], blendFactor);
            }
            
            return finalVisibility; 
        }

        // 用于点光源 PCF 的采样偏移向量 (20个方向)
        vec3 gridSamplingDisk[20] = vec3[](
           vec3(1, 1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1, 1,  1), 
           vec3(1, 1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1, 1, -1),
           vec3(1, 1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1, 1,  0),
           vec3(1, 0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1, 0, -1),
           vec3(0, 1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0, 1, -1)
        );

        // 点光源阴影计算函数
        float CalcPointShadow(vec3 fragPos, vec3 lightPos, int shadowIndex, float range, float radius, float bias)
        {
            // 注意：我们之前用 uniform 传了 pointShadowFarPlanes
            // 但其实对于现代物理光照，Range 本身就是 FarPlane (光照在 Range 处归零)
            // 所以我们可以直接用 light.range 作为远平面
            float farPlane = range; 
            
            vec3 fragToLight = fragPos - lightPos;
            float currentDepth = length(fragToLight);
            
            float shadow = 0.0;
            int samples = 20;
            float viewDistance = length(viewPos - fragPos);
            
            // 使用 radius 参数控制模糊程度
            // 公式：(1 + viewDist/range) * radius / divider
            // 用户输入的 radius 是 0.0 ~ 0.5
            float diskRadius = (1.0 + (viewDistance / farPlane)) * radius; 
            
            float rotX = random(vec3(gl_FragCoord.xy, 1.0), 1);
            float rotY = random(vec3(gl_FragCoord.xy, 1.0), 2);
            float rotZ = random(vec3(gl_FragCoord.xy, 1.0), 3);
            vec3 rotationDir = normalize(vec3(rotX, rotY, rotZ)); 
            
            for(int i = 0; i < samples; ++i)
            {
                float closestDepth = 0.0;
                vec3 sampleOffset = reflect(gridSamplingDisk[i], rotationDir);
                vec3 sampleDir = fragToLight + sampleOffset * diskRadius;

                if (shadowIndex == 0) closestDepth = texture(pointShadowMaps[0], sampleDir).r;
                else if (shadowIndex == 1) closestDepth = texture(pointShadowMaps[1], sampleDir).r;
                else if (shadowIndex == 2) closestDepth = texture(pointShadowMaps[2], sampleDir).r;
                else if (shadowIndex == 3) closestDepth = texture(pointShadowMaps[3], sampleDir).r;
                
                closestDepth *= farPlane;
                
                if(currentDepth - bias > closestDepth)
                    shadow += 1.0;
            }
            shadow /= float(samples);
            return 1.0 - shadow;
        }

        // 物理正确的窗口化反平方衰减
        // distance: 像素到光源距离
        // range: 光源设定的半径
        float GetAttenuation(float distance, float range) {
            // 1. 基础反平方 ( Inverse Square Law )
            // 加一个小数值防止除零 (0.01 或 1.0 取决于单位，这里假设 1 unit = 1 meter)
            float attenuation = 1.0 / (distance * distance + 1.0); 
            
            // 2. 窗口函数 (Windowing Function) - 让光在 range 处平滑归零
            // 来自 Unreal Engine / Frostbite 的公式
            float distDivRange = distance / range;
            float factor = distDivRange * distDivRange; // (d/r)^2
            factor = factor * factor;                   // (d/r)^4
            float window = clamp(1.0 - factor, 0.0, 1.0);
            
            return attenuation * window * window;
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

    // 4. 初始化 ShadowMapPass
    _shadowPass = std::make_unique<ShadowMapPass>(4096);

    // 5. 初始化 PointShadowPass
    // 分辨率 1024，最大支持 4 个点光源
    _pointShadowPass = std::make_unique<PointShadowPass>(1024, 4);
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
    // Pass -1: 烘焙反射探针 (Bake Reflection Probes)
    // 在渲染主画面之前，先更新场景里的“镜子”所看到的景象
    updateReflectionProbes(scene);

    // ===============================================
    // Pass -0.5: 阴影准备与渲染
    // ===============================================
    
    // 1. 收集并分类光源
    std::vector<LightComponent*> dirLights;
    std::vector<LightComponent*> pointLights;
    std::vector<LightComponent*> spotLights;
    
    // 用于传递给 ShadowPass 的纯数据
    std::vector<ShadowCasterInfo> csmCasters; // 平行光
    std::vector<PointShadowInfo> pointShadowInfos; // 点光源
    
    // 记录光源组件对应的纹理层级索引 (LightComponent* -> LayerIndex)
    std::unordered_map<LightComponent*, int> lightToShadowIndex;

    int csmLayersPerLight = _shadowPass->getCascadeCount(); // 通常是 5 (4级联 + 1)
    
    // 遍历场景收集光源
    for (const auto& go : scene.getGameObjects()) {
        auto light = go->getComponent<LightComponent>();
        if (light && light->enabled) {
            if (light->type == LightType::Directional) {
                dirLights.push_back(light);
                
                // 判断是否投射阴影 (且未超过最大限制，假设 ShadowPass 支持 4 个)
                // 注意：这里 4 必须与 ShadowMapPass 构造时的 maxLights 一致
                if (light->castShadows && csmCasters.size() < 4) {
                    ShadowCasterInfo info;
                    // 计算光的方向 (物体的前方是 -Z，应用旋转)
                    info.direction = go->transform.rotation * glm::vec3(0, 0, -1);
                    info.shadowNormalBias = light->shadowNormalBias;
                    info.cullFaceMode = light->shadowCullFace;
                    
                    csmCasters.push_back(info);
                    
                    // 计算该光源在 TextureArray 中的起始层级
                    // 第 0 个光源用 0~4 层，第 1 个用 5~9 层...
                    int baseLayer = (int)(csmCasters.size() - 1) * csmLayersPerLight;
                    lightToShadowIndex[light] = baseLayer;
                } else {
                    lightToShadowIndex[light] = -1; // 不投射阴影
                }
            }
            else if (light->type == LightType::Point) {
                pointLights.push_back(light);

                // 检查是否开启阴影且未超限 (PointShadowPass 最大支持 4 个)
                if (light->castShadows && pointShadowInfos.size() < _pointShadowPass->getMaxLights()) {
                    PointShadowInfo info;
                    info.position = go->transform.position;
                    info.farPlane = light->range;
                    info.lightIndex = (int)pointShadowInfos.size(); // 0, 1, 2, 3...

                    pointShadowInfos.push_back(info);
                    lightToShadowIndex[light] = info.lightIndex;
                } else {
                    lightToShadowIndex[light] = -1;
                }
            }
            else if (light->type == LightType::Spot) {
                spotLights.push_back(light);
            }
        }
    }

    // 2. 执行 Shadow Passes
    // 渲染平行光 (CSM)
    _shadowPass->render(scene, csmCasters, camera);
    
    // 渲染点光源 (Omnidirectional)
    _pointShadowPass->render(scene, pointShadowInfos);

    // ===============================================
    // Pass 1: 主场景渲染
    // ===============================================

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

    // 绑定阴影纹理数组到 Slot 2
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, _shadowPass->getDepthMapArray());

    // 绑定 Point Shadow Cubemaps 到 Slot 4, 5, 6, 7
    // Slot 0: Diffuse, Slot 1: Env, Slot 2: CSM, Slot 3: Reserved
    for (int i = 0; i < pointShadowInfos.size(); ++i) {
        glActiveTexture(GL_TEXTURE4 + i);
        glBindTexture(GL_TEXTURE_CUBE_MAP, _pointShadowPass->getShadowMap(i));
    }

    // 绘制天空盒
    drawSkybox(view, proj);

    // 绘制场景物体 (传入收集好的光源数据)
    drawSceneObjects(scene, view, proj, viewPos, dirLights, pointLights, spotLights, lightToShadowIndex);

    // 绘制网格
    drawGrid(view, proj, viewPos);

    // 绘制描边
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

void Renderer::drawSceneObjects(const Scene& scene, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& viewPos, 
                                const std::vector<LightComponent*>& dirLights,
                                const std::vector<LightComponent*>& pointLights,
                                const std::vector<LightComponent*>& spotLights,
                                const std::unordered_map<LightComponent*, int>& shadowIndices,
                                const GameObject* excludeObject)
{
    _mainShader->use();
    _mainShader->setUniformMat4("projection", proj);
    _mainShader->setUniformMat4("view", view);
    _mainShader->setUniformVec3("viewPos", viewPos);

    _mainShader->setUniformBool("isDebug", false);

    // 1. 设置阴影相关 Uniforms
    _mainShader->setUniformInt("shadowMap", 2);

    // 传递矩阵数组
    const auto& matrices = _shadowPass->getLightSpaceMatrices();
    if (!matrices.empty()) {
        GLint loc = glGetUniformLocation(_mainShader->getHandle(), "lightSpaceMatrices");
        if (loc != -1) {
            glUniformMatrix4fv(loc, (GLsizei)matrices.size(), GL_FALSE, glm::value_ptr(matrices[0]));
        }
    }

    // 传递级联分割距离
    const auto& levels = _shadowPass->getCascadeLevels();
    if (!levels.empty()) {
        GLint loc = glGetUniformLocation(_mainShader->getHandle(), "cascadePlaneDistances");
        if (loc != -1) {
            glUniform1fv(loc, (GLsizei)levels.size(), levels.data());
        }
        _mainShader->setUniformInt("cascadeCount", (int)levels.size());
    }

    // 我们假设所有灯光共享同一个 shadowBias (或者取第一个开启阴影的灯的配置)
    // 如果想每个灯不同，需要在 shader struct 里加 bias 字段
    // 这里简单处理，取一个默认值或第一个灯的值
    float globalBias = 0.001f;
    for(auto l : dirLights) if(l->castShadows) { globalBias = l->shadowBias; break; }
    _mainShader->setUniformFloat("shadowBias", globalBias);

    // 2. Point Shadows -> Slot 4, 5, 6, 7
    // 告诉 Shader：pointShadowMaps[0] 在纹理单元 4，[1] 在 5...
    int pointShadowSamplers[4] = {4, 5, 6, 7};
    GLint locPointMaps = glGetUniformLocation(_mainShader->getHandle(), "pointShadowMaps");
    if (locPointMaps != -1) {
        // 传递数组
        glUniform1iv(locPointMaps, 4, pointShadowSamplers);
    }

    // 传递 Far Planes (用于深度归一化)
    // 我们需要在 shadowIndices 中查找哪些灯开启了阴影，并收集它们的 FarPlane
    // 这里简单处理：我们在 render() 里硬编码了 50.0f，这里需要保持一致
    // 更好的做法是将 farPlane 存在 LightComponent 或者 render() 传进来
    float pointShadowFarPlanes[4] = {50.0f, 50.0f, 50.0f, 50.0f}; 
    GLint locFarPlanes = glGetUniformLocation(_mainShader->getHandle(), "pointShadowFarPlanes");
    if (locFarPlanes != -1) {
        glUniform1fv(locFarPlanes, 4, pointShadowFarPlanes);
    }

    // 2. 提交光源数据
    int maxDir = 4; // Shader 中定义的 NR_DIR_LIGHTS
    int countDir = 0;
    for (auto light : dirLights) {
        if (countDir >= maxDir) break;
        std::string base = "dirLights[" + std::to_string(countDir) + "]";
        
        glm::vec3 dir = light->owner->transform.rotation * glm::vec3(0, 0, -1);
        _mainShader->setUniformVec3(base + ".direction", dir);
        _mainShader->setUniformVec3(base + ".color", light->color);
        _mainShader->setUniformFloat(base + ".intensity", light->intensity);
        
        // 设置 Shadow Index
        int idx = -1;
        if (shadowIndices.count(light)) {
            idx = shadowIndices.at(light);
        }
        _mainShader->setUniformInt(base + ".shadowIndex", idx);
        
        countDir++;
    }
    _mainShader->setUniformInt("dirLightCount", countDir);

    int maxPoint = 4;
    int countPoint = 0;
    for (auto light : pointLights) {
        if (countPoint >= maxPoint) break;
        std::string base = "pointLights[" + std::to_string(countPoint) + "]";
        _mainShader->setUniformVec3(base + ".position", light->owner->transform.position);
        _mainShader->setUniformVec3(base + ".color", light->color);
        _mainShader->setUniformFloat(base + ".intensity", light->intensity);
        _mainShader->setUniformFloat(base + ".range", light->range);

        int idx = -1;
        if (shadowIndices.count(light)) idx = shadowIndices.at(light);
        _mainShader->setUniformInt(base + ".shadowIndex", idx);

        // 即使 idx == -1 (无阴影)，也可以传进去，反正 Shader 里有 if 判断
        _mainShader->setUniformFloat(base + ".shadowStrength", light->shadowStrength);
        _mainShader->setUniformFloat(base + ".shadowRadius", light->shadowRadius);
        _mainShader->setUniformFloat(base + ".shadowBias", light->shadowBias);
        
        // 顺便更新 Gizmo 颜色 (可选)
        if (auto mesh = light->owner->getComponent<MeshComponent>()) {
            if (mesh->isGizmo) mesh->material.diffuse = light->color;
        }
        countPoint++;
    }
    _mainShader->setUniformInt("pointLightCount", countPoint);

    int maxSpot = 4;
    int countSpot = 0;
    for (auto light : spotLights) {
        if (countSpot >= maxSpot) break;
        std::string base = "spotLights[" + std::to_string(countSpot) + "]";
        glm::vec3 dir = light->owner->transform.rotation * glm::vec3(0, 0, -1);
        
        _mainShader->setUniformVec3(base + ".position", light->owner->transform.position);
        _mainShader->setUniformVec3(base + ".direction", dir);
        _mainShader->setUniformVec3(base + ".color", light->color);
        _mainShader->setUniformFloat(base + ".intensity", light->intensity);
        _mainShader->setUniformFloat(base + ".cutOff", light->cutOff);
        _mainShader->setUniformFloat(base + ".outerCutOff", light->outerCutOff);
        _mainShader->setUniformFloat(base + ".range", light->range);
        
        if (auto mesh = light->owner->getComponent<MeshComponent>()) {
            if (mesh->isGizmo) mesh->material.diffuse = light->color;
        }
        countSpot++;
    }
    _mainShader->setUniformInt("spotLightCount", countSpot);

    _mainShader->setUniformInt("diffuseMap", 0);

    // 3. 绘制物体 Loop
    glFrontFace(GL_CCW);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    for (const auto& go : scene.getGameObjects()) {
        // 排除特定物体 (防止烘焙时自己遮挡自己)
        if (excludeObject && go.get() == excludeObject) continue;

        auto meshComp = go->getComponent<MeshComponent>();
        if (!meshComp || !meshComp->enabled) continue;
        
        auto lightComp = go->getComponent<LightComponent>();
        
        // 双面渲染处理
        if (meshComp->doubleSided) glDisable(GL_CULL_FACE);

        // 纹理绑定
        if (meshComp->diffuseMap) {
            meshComp->diffuseMap->bind(0); // Bind to Slot 0
            _mainShader->setUniformBool("hasDiffuseMap", true);
        } else {
            _mainShader->setUniformBool("hasDiffuseMap", false);
            // 为了安全，解绑 Slot 0 或者绑定一个白色纹理
            // Texture2D::unbind() 是静态的或者解绑当前，这里简单处理：
            // 只要 hasDiffuseMap 是 false，Shader 就不会采样，所以不解绑也行，
            // 但为了防止状态污染，最好解绑。
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        
        _mainShader->setUniformBool("isUnlit", meshComp->isGizmo);
        _mainShader->setUniformBool("isDoubleSided", meshComp->doubleSided);
        _mainShader->setUniformVec3("material.ambient", meshComp->material.ambient);
        _mainShader->setUniformVec3("material.diffuse", meshComp->material.diffuse);
        _mainShader->setUniformVec3("material.specular", meshComp->material.specular);
        _mainShader->setUniformFloat("material.shininess", meshComp->material.shininess);

        _mainShader->setUniformFloat("material.reflectivity", meshComp->material.reflectivity);
        _mainShader->setUniformFloat("material.refractionIndex", meshComp->material.refractionIndex);
        _mainShader->setUniformFloat("material.transparency", meshComp->material.transparency);

        _mainShader->setUniformBool("useTriplanar", meshComp->useTriplanar);
        _mainShader->setUniformFloat("triplanarScale", meshComp->triplanarScale);

        auto probe = go->getComponent<ReflectionProbeComponent>();
        if (probe && probe->textureID != 0) {
            // 如果这个物体本身就是一个反射探针，我们就使用它生成的 Environment Map
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_CUBE_MAP, probe->textureID);
            
            _mainShader->setUniformBool("hasEnvMap", true);
            _mainShader->setUniformInt("envMap", 1); // 告诉 Shader 去纹理单元 1 找
            
            // 计算并传递 Box 参数
            // 假设探针中心就是物体中心
            glm::vec3 pPos = go->transform.position;
            // 计算世界坐标下的 AABB
            glm::vec3 bMin = pPos - probe->boxSize * 0.5f;
            glm::vec3 bMax = pPos + probe->boxSize * 0.5f;

            _mainShader->setUniformVec3("probePos", pPos);
            _mainShader->setUniformVec3("probeBoxMin", bMin);
            _mainShader->setUniformVec3("probeBoxMax", bMax);
        } else {
            _mainShader->setUniformBool("hasEnvMap", false);
        }

        // 计算 Model Matrix
        glm::mat4 modelMatrix = go->transform.getLocalMatrix();
        modelMatrix = modelMatrix * meshComp->model->transform.getLocalMatrix();

        _mainShader->setUniformMat4("model", modelMatrix);
        
        meshComp->model->draw();

        if (meshComp->doubleSided) glEnable(GL_CULL_FACE);
    }
}

void Renderer::updateReflectionProbes(const Scene& scene)
{
    // 获取当前视口，以便烘焙完后恢复
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    // 1. 预先收集光源 (为了简单起见，反射探针渲染时不开启阴影)
    std::vector<LightComponent*> dirLights, pointLights, spotLights;
    std::unordered_map<LightComponent*, int> emptyShadowIndices; // 空 map，表示无阴影

    for (const auto& go : scene.getGameObjects()) {
        auto light = go->getComponent<LightComponent>();
        if (light && light->enabled) {
            if (light->type == LightType::Directional) dirLights.push_back(light);
            else if (light->type == LightType::Point) pointLights.push_back(light);
            else if (light->type == LightType::Spot) spotLights.push_back(light);
        }
    }

    // 遍历所有物体，找带 ReflectionProbeComponent 的
    for (const auto& go : scene.getGameObjects())
    {
        auto probe = go->getComponent<ReflectionProbeComponent>();
        if (!probe) continue;

        // 1. 确保 GL 资源已创建
        probe->initGL();

        // 2. 准备烘焙参数
        glBindFramebuffer(GL_FRAMEBUFFER, probe->fboID);
        glViewport(0, 0, probe->resolution, probe->resolution);

        glm::vec3 probePos = go->transform.position;
        // 投影矩阵：90度 FOV, 1:1 比例, 近裁剪面 0.1, 远裁剪面 100
        glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);

        // 3. 朝 6 个方向渲染
        // OpenGL Cubemap 面顺序: +X, -X, +Y, -Y, +Z, -Z
        std::vector<glm::mat4> shadowViews;
        shadowViews.push_back(glm::lookAt(probePos, probePos + glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)));
        shadowViews.push_back(glm::lookAt(probePos, probePos + glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)));
        shadowViews.push_back(glm::lookAt(probePos, probePos + glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)));
        shadowViews.push_back(glm::lookAt(probePos, probePos + glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)));
        shadowViews.push_back(glm::lookAt(probePos, probePos + glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)));
        shadowViews.push_back(glm::lookAt(probePos, probePos + glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)));

        for (int i = 0; i < 6; ++i)
        {
            // 将 FBO 颜色附件绑定到 Cubemap 的当前面
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, probe->textureID, 0);
            
            // 清屏 (注意：这里不需要 glClearColor 设置太亮，否则缝隙会明显)
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // A. 画天空盒
            // 注意：DrawSkybox 需要去平移的 View 矩阵
            glm::mat4 viewNoTrans = glm::mat4(glm::mat3(shadowViews[i])); 
            drawSkybox(viewNoTrans, shadowProj);

            // B. 画场景物体
            // 关键：传入 go.get() 作为 excludeObject，防止画自己
            drawSceneObjects(scene, shadowViews[i], shadowProj, probePos, 
                             dirLights, pointLights, spotLights, emptyShadowIndices, 
                             go.get());
        }

        // 6个面都画完了，生成 Mipmap
        glBindTexture(GL_TEXTURE_CUBE_MAP, probe->textureID);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    }

    // 恢复状态
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}