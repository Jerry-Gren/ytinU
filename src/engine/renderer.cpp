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
        layout(location = 3) in vec3 aTangent;

        out vec3 FragPos;
        out vec3 Normal;
        out vec2 TexCoord;
        out mat3 TBN;

        out vec3 LocalPos;
        // 输出视空间深度 (或者直接用 gl_Position.w)
        out float ClipSpaceZ;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        void main() {
            vec4 worldPos = model * vec4(aPosition, 1.0);
            FragPos = vec3(worldPos);
            
            // 1. 计算 Normal Matrix (法线矩阵)
            // 它可以处理非均匀缩放，保证法线方向正确
            mat3 normalMatrix = mat3(transpose(inverse(model)));

            // 2. 计算世界空间法线 (N)
            vec3 N = normalize(normalMatrix * aNormal);
            Normal = N; // 将计算好的法线传给 FS (虽然 FS 可能有了 TBN 会重算，但保留它是个好习惯)
            
            // 3. 计算世界空间切线 (T)
            vec3 T = normalize(normalMatrix * aTangent);
            
            // 4. Gram-Schmidt 正交化
            // 这一步非常关键！它剔除 T 中包含的 N 分量，确保 T 绝对垂直于 N。
            // 这样可以防止因精度问题或模型数据不佳导致的 TBN 变形。
            T = normalize(T - dot(T, N) * N);
            
            // 5. 计算副切线 (Bitangent, B)
            // 利用叉乘生成第三个轴
            vec3 B = cross(N, T);
            
            // 6. 构建 TBN 矩阵
            TBN = mat3(T, B, N);

            TexCoord = aTexCoord;
            LocalPos = aPosition;
            
            gl_Position = projection * view * worldPos;
            
            // 保存 View Space 的深度
            ClipSpaceZ = gl_Position.w;
        }
    )";

    // 片元着色器 (Fragment Shader) - Blinn-Phong 多光源版本
    const char *fsCode = R"(
        #version 330 core
        #extension GL_ARB_shader_texture_lod : enable
        out vec4 FragColor;

        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;
        in vec3 LocalPos;
        in float ClipSpaceZ;
        in mat3 TBN;

        // 材质定义
        struct Material {
            vec3 albedo;
            float metallic;
            float roughness;
            float ao;

            float reflectivity;
            float refractionIndex;
            float transparency;
        };

        uniform Material material;

        // 纹理
        uniform sampler2D diffuseMap; 
        uniform bool hasDiffuseMap;
        uniform bool useTriplanar;
        uniform float triplanarScale;
        uniform vec3 triRotPos; // x,y,z 对应 +X,+Y,+Z 面的角度
        uniform vec3 triRotNeg; // x,y,z 对应 -X,-Y,-Z 面的角度
        uniform vec3 triFlipPos;
        uniform vec3 triFlipNeg;
        // 法线贴图
        uniform sampler2D normalMap;
        uniform bool hasNormalMap;
        uniform float normalStrength; // 默认 1.0
        uniform bool flipNormalY;     // 默认 false

        // 动态环境贴图 IBL
        uniform samplerCube envMap;
        uniform bool hasEnvMap;
        uniform float iblIntensity;

        // 视差校正
        uniform vec3 probePos;    // 探针拍摄时的中心位置 (世界坐标)
        uniform vec3 probeBoxMin; // 房间的最小边界 (世界坐标)
        uniform vec3 probeBoxMax; // 房间的最大边界 (世界坐标)

        // 用于在函数间传递 Triplanar 计算结果
        struct TriplanarData {
            vec2 uvX;
            vec2 uvY;
            vec2 uvZ;
            vec3 blend;
        };

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
        
        uniform DirLight dirLights[NR_DIR_LIGHTS];
        uniform int dirLightCount;

        uniform PointLight pointLights[NR_POINT_LIGHTS];
        uniform int pointLightCount;

        uniform SpotLight spotLights[NR_SPOT_LIGHTS];
        uniform int spotLightCount;

        // CSM (平行光) 阴影
        uniform sampler2DArrayShadow shadowMap; 
        // 假设最大 4 个灯 * 6 层级联 = 24 个矩阵
        // 为安全起见定义 32
        uniform mat4 lightSpaceMatrices[32];
        uniform float cascadePlaneDistances[16];
        uniform int cascadeCount;
        uniform float shadowBias;

        // 点光源阴影
        uniform samplerCube pointShadowMaps[NR_POINT_SHADOWS];
        uniform float pointShadowFarPlanes[NR_POINT_SHADOWS];

        const float PI = 3.14159265359;

        // 1. 法线分布函数 (NDF) - GGX Trowbridge-Reitz
        // 决定了高光的大小和形状 (Roughness 越小，光斑越集中)
        float DistributionGGX(vec3 N, vec3 H, float roughness)
        {
            float a = roughness * roughness;
            float a2 = a * a;
            float NdotH = max(dot(N, H), 0.0);
            float NdotH2 = NdotH * NdotH;

            float nom   = a2;
            float denom = (NdotH2 * (a2 - 1.0) + 1.0);
            denom = PI * denom * denom;

            return nom / max(denom, 0.0000001); // 防止除零
        }

        // 2. 几何遮蔽函数 (Geometry) - Schlick-GGX
        // 模拟粗糙表面的微观自遮挡 (Roughness 越大，越暗)
        float GeometrySchlickGGX(float NdotV, float roughness)
        {
            // 对于直接光照，k 取值如下：
            float r = (roughness + 1.0);
            float k = (r*r) / 8.0;

            float nom   = NdotV;
            float denom = NdotV * (1.0 - k) + k;

            return nom / max(denom, 0.0000001);
        }

        float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
        {
            float NdotV = max(dot(N, V), 0.0);
            float NdotL = max(dot(N, L), 0.0);
            float ggx2 = GeometrySchlickGGX(NdotV, roughness);
            float ggx1 = GeometrySchlickGGX(NdotL, roughness);

            return ggx1 * ggx2;
        }

        // 3. 菲涅尔方程 (Fresnel) - Schlick 近似
        // 决定了反射光和折射光(漫反射)的比例
        // F0: 基础反射率 (非金属0.04, 金属为 albedo)
        vec3 FresnelSchlick(float cosTheta, vec3 F0)
        {
            return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
        }

        // 4. 针对 IBL 的菲涅尔函数 (加入粗糙度影响)
        // 越粗糙的表面，边缘的菲涅尔反射越弱
        vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
        {
            return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
        }
        
        // 5. 统一的 PBR 计算核心
        // 输入：光方向L, 视线V, 法线N, 光照强度Radiance, 材质F0, 材质属性
        vec3 CalculatePBR_Lo(vec3 L, vec3 V, vec3 N, vec3 radiance, vec3 F0, vec3 albedo, float roughness, float metallic)
        {
            vec3 H = normalize(V + L); // 半程向量

            // Cook-Torrance BRDF
            float NDF = DistributionGGX(N, H, roughness);   
            float G   = GeometrySmith(N, V, L, roughness);      
            vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0);
           
            vec3 numerator    = NDF * G * F; 
            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // +0.0001 防止除零
            vec3 specular = numerator / denominator;
            
            // kS 是菲涅尔反射部分 (即 F)
            vec3 kS = F;
            // kD 是剩下的漫反射部分 (能量守恒: 入射光 - 反射光)
            vec3 kD = vec3(1.0) - kS;
            // 金属没有漫反射 (被自由电子吸收)，所以乘以 (1 - metallic)
            kD *= 1.0 - metallic;	  

            // 最终出射光 Lo = (漫反射 + 镜面反射) * 辐射率 * cosTheta
            float NdotL = max(dot(N, L), 0.0);
            
            return (kD * albedo / PI + specular) * radiance * NdotL;
        }

        // 6. 环境光 BRDF 近似计算 (替代 BRDF LUT 贴图)
        // 这是 Karis (Epic Games) 提出的拟合公式，极其经典
        vec3 EnvBRDFApprox(vec3 specularColor, float roughness, float NdotV)
        {
            const vec4 c0 = vec4(-1, -0.0275, -0.572, 0.022);
            const vec4 c1 = vec4(1, 0.0425, 1.04, -0.04);
            vec4 r = roughness * c0 + c1;
            float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
            vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
            return specularColor * AB.x + AB.y;
        }

        // 函数声明
        vec4 getTriplanarSample(vec3 worldPos, vec3 normal);

        void CalcDirLight(DirLight light, vec3 N, vec3 V, vec3 albedo, vec3 F0, float shadow, inout vec3 diffAccum, inout vec3 specAccum);
        void CalcPointLight(PointLight light, vec3 N, vec3 pos, vec3 V, vec3 albedo, vec3 F0, float shadow, inout vec3 diffAccum, inout vec3 specAccum);
        void CalcSpotLight(SpotLight light, vec3 N, vec3 pos, vec3 V, vec3 albedo, vec3 F0, inout vec3 diffAccum, inout vec3 specAccum);

        vec3 BoxProjectedCubemapDirection(vec3 worldPos, vec3 worldRefDir, vec3 pPos, vec3 boxMin, vec3 boxMax);

        float ShadowCalculation(vec3 fragPosWorld, vec3 normal, vec3 lightDir, float viewSpaceDepth, int baseLayerIndex);
        float CalcPointShadow(vec3 fragPos, vec3 lightPos, int shadowIndex, float range, float radius, float bias);

        float GetAttenuation(float distance, float range);

        TriplanarData CalcTriplanarData(vec3 position, vec3 normal);
        vec4 SampleTriplanar(sampler2D theMap, TriplanarData data);
        vec3 SampleTriplanarNormal(sampler2D normMap, TriplanarData data, vec3 worldNormal);

		// 获取法线辅助函数
		vec3 getNormal() {
            vec3 n = normalize(Normal);
            if (isDoubleSided && !gl_FrontFacing) n = -n; 
            return n;
        }

        // ACES Tone Mapping
        vec3 ACESFilm(vec3 x) {
            float a = 2.51f;
            float b = 0.03f;
            float c = 2.43f;
            float d = 0.59f;
            float e = 0.14f;
            return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
        }
        
        void main() {
            vec3 norm = getNormal();

            TriplanarData triData;
            if (useTriplanar) {
                // 使用 LocalPos 保证纹理随物体移动
                triData = CalcTriplanarData(LocalPos, norm); 
            }

            vec3 albedoColor = material.albedo;

            // 基础纹理采样
            if (hasDiffuseMap) {
                vec4 texColor;
                if (useTriplanar) {
                    texColor = SampleTriplanar(diffuseMap, triData);
                } else {
                    texColor = texture(diffuseMap, TexCoord);
                }
                // sRGB 矫正
                texColor.rgb = pow(texColor.rgb, vec3(2.2)); 
                albedoColor = texColor.rgb * material.albedo;
            }

            if (hasNormalMap) {
                if (useTriplanar) {
                    // 使用专门的 Triplanar 法线混合函数
                    // 注意：这里传入的是几何法线 Normal (大写的)，不是 TBN 计算出的
                    norm = SampleTriplanarNormal(normalMap, triData, normalize(Normal));
                } else {
                    // 标准 UV 法线 (使用 getNormal 里的逻辑，可以直接把代码搬过来或保持原样)
                    // ... 之前的 TBN 逻辑 ...
                    vec3 rawNormal = texture(normalMap, TexCoord).rgb;
                    if (flipNormalY) rawNormal.g = 1.0 - rawNormal.g;
                    vec3 tangentNormal = rawNormal * 2.0 - 1.0;
                    tangentNormal.xy *= normalStrength;
                    norm = normalize(TBN * normalize(tangentNormal));
                    if (isDoubleSided && !gl_FrontFacing) norm = -norm; 
                }
            } else {
                // 如果没有贴图，确保 norm 已经处理了双面渲染
                if (isDoubleSided && !gl_FrontFacing) norm = -norm; 
            }

            // Unlit 模式直接返回
            if (isUnlit) {
                FragColor = vec4(albedoColor, 1.0); 
                return;
            }
            
            vec3 viewDir = normalize(viewPos - FragPos);

            // ================= PBR 参数准备 =================
            // F0: 基础反射率 (0度角的反射率)
            // 绝缘体(非金属) F0 约为 0.04
            // 导体(金属) F0 为自身的 albedo 颜色
            vec3 F0 = vec3(0.04); 
            F0 = mix(F0, albedoColor, material.metallic);
            // ==============================================

            // 分离 Diffuse 和 Specular 累加器
            vec3 directDiffuse = vec3(0.0);
            vec3 directSpecular = vec3(0.0);

            // 1. 计算所有直接光照
            for(int i = 0; i < dirLightCount; i++) {
                float shadow = 1.0;
                if (dirLights[i].shadowIndex >= 0) {
                    vec3 lightDir = normalize(-dirLights[i].direction);
                    shadow = ShadowCalculation(FragPos, norm, lightDir, ClipSpaceZ, dirLights[i].shadowIndex);
                }
                CalcDirLight(dirLights[i], norm, viewDir, albedoColor, F0, shadow, directDiffuse, directSpecular);
            }
            
            for(int i = 0; i < pointLightCount; i++) {
                float shadow = 1.0;
                if (pointLights[i].shadowIndex >= 0) {
                    float rawShadow = CalcPointShadow(FragPos, pointLights[i].position, pointLights[i].shadowIndex, pointLights[i].range, pointLights[i].shadowRadius, pointLights[i].shadowBias);
                    shadow = mix(1.0, rawShadow, pointLights[i].shadowStrength);
                }
                CalcPointLight(pointLights[i], norm, FragPos, viewDir, albedoColor, F0, shadow, directDiffuse, directSpecular);
            }
            
            for(int i = 0; i < spotLightCount; i++)
                CalcSpotLight(spotLights[i], norm, FragPos, viewDir, albedoColor, F0, directDiffuse, directSpecular);
            
            // 2. 环境光 (IBL)
            vec3 ambientDiffuse = vec3(0.0);
            vec3 ambientSpecular = vec3(0.0);
            
            if (hasEnvMap) {
                // Diffuse IBL
                vec3 irradiance = textureLod(envMap, norm, 8.0).rgb; 
                vec3 kS = FresnelSchlickRoughness(max(dot(norm, viewDir), 0.0), F0, material.roughness);
                vec3 kD = 1.0 - kS;
                kD *= 1.0 - material.metallic;
                
                ambientDiffuse = kD * irradiance * albedoColor * iblIntensity;

                // Specular IBL
                vec3 R = reflect(-viewDir, norm);
                vec3 correctedR = BoxProjectedCubemapDirection(FragPos, R, probePos, probeBoxMin, probeBoxMax);
                float MAX_LOD = 4.0;
                vec3 prefilteredColor = textureLod(envMap, correctedR, material.roughness * MAX_LOD).rgb;
                vec3 specularFactor = EnvBRDFApprox(F0, material.roughness, max(dot(norm, viewDir), 0.0));
                
                ambientSpecular = prefilteredColor * specularFactor * iblIntensity;
            } else {
                ambientDiffuse = vec3(0.03) * albedoColor * material.ao; 
            }
            
            // 3. 组合 PBR 不透明部分
            vec3 opaqueColor = (ambientDiffuse + directDiffuse) * material.ao + (ambientSpecular + directSpecular);
            
            // 4. 玻璃/折射逻辑 (关键修改)
            vec3 finalColor = opaqueColor;

            if (material.transparency > 0.001) 
            {
                // 计算玻璃的 Fresnel (使用用户指定的 Reflectivity 微调 F0)
                float glassF0 = mix(0.0, 0.5, material.reflectivity); // 注意：这里范围改小了
                // 或者更物理一点，假设玻璃 F0 固定 0.04，Reflectivity 只控制额外增强
                vec3 F0_Glass = vec3(0.04); 
                
                float cosTheta = clamp(dot(norm, -normalize(FragPos - viewPos)), 0.0, 1.0);
                vec3 F = FresnelSchlick(cosTheta, F0_Glass);
                
                // 用户 reflectivity 额外增强 Fresnel 效果 (艺术控制)
                F += material.reflectivity * 0.5;
                F = clamp(F, 0.0, 1.0);

                // 折射与反射
                vec3 refractColor = vec3(0.0);
                vec3 reflectColor = vec3(0.0);

                if (hasEnvMap) {
                    // Refract
                    float k = max(material.refractionIndex, 1.0);
                    float ratio = 1.0 / k;
                    vec3 I = normalize(FragPos - viewPos);
                    
                    // 简化折射采样 (不做色散了，为了性能和清晰度)
                    vec3 R_refract = refract(I, norm, ratio);
                    vec3 corrR_refract = BoxProjectedCubemapDirection(FragPos, R_refract, probePos, probeBoxMin, probeBoxMax);
                    refractColor = textureLod(envMap, corrR_refract, 0.0).rgb * iblIntensity;

                    // Reflect
                    vec3 R_reflect = reflect(I, norm);
                    vec3 corrR_reflect = BoxProjectedCubemapDirection(FragPos, R_reflect, probePos, probeBoxMin, probeBoxMax);
                    reflectColor = textureLod(envMap, corrR_reflect, material.roughness * 4.0).rgb * iblIntensity;
                }

                vec3 glassBody = mix(refractColor, reflectColor, F);
                
                // [关键] 混合: 玻璃本体 + 直接光高光 (Sun Specular)
                // 即使玻璃是透明的，太阳照上去也应该有亮斑 (directSpecular)
                // 而 diffuse 部分被 transparency 替换掉了
                
                vec3 glassResult = glassBody + directSpecular;

                finalColor = mix(opaqueColor, glassResult, material.transparency);
            }

            // 5. Tone Mapping (ACES)
            finalColor = ACESFilm(finalColor);
            
            // 6. Gamma
            finalColor = pow(finalColor, vec3(1.0/2.2));

            FragColor = vec4(finalColor, 1.0);

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

        vec2 rotateUV(vec2 uv, float angleDeg) {
            vec2 center = vec2(0.5);
            uv -= center;
            float rad = radians(angleDeg);
            float s = sin(rad);
            float c = cos(rad);
            mat2 rotMat = mat2(c, -s, s, c);
            uv = rotMat * uv;
            uv += center;
            return uv;
        }

        TriplanarData CalcTriplanarData(vec3 position, vec3 normal) {
            TriplanarData data;
            
            // 1. 权重
            vec3 blending = abs(normal);
            blending = pow(blending, vec3(4.0)); 
            float b = (blending.x + blending.y + blending.z);
            data.blend = blending / vec3(b, b, b);

            // 2. 基础投影
            data.uvX = position.zy * triplanarScale + 0.5;
            data.uvY = position.xz * triplanarScale + 0.5;
            data.uvZ = position.xy * triplanarScale + 0.5;

            // 3. 旋转 (基于轴向)
            // X轴
            bool isPosX = normal.x > 0.0;
            float rotX = isPosX ? triRotPos.x : triRotNeg.x;
            if (rotX > 0.1) data.uvX = rotateUV(data.uvX, rotX);
            
            // Y轴
            bool isPosY = normal.y > 0.0;
            float rotY = isPosY ? triRotPos.y : triRotNeg.y;
            if (rotY > 0.1) data.uvY = rotateUV(data.uvY, rotY);

            // Z轴
            bool isPosZ = normal.z > 0.0;
            float rotZ = isPosZ ? triRotPos.z : triRotNeg.z;
            if (rotZ > 0.1) data.uvZ = rotateUV(data.uvZ, rotZ);

            // 4. 翻转 (基于轴向)
            bool flipX = isPosX ? (triFlipPos.x > 0.5) : (triFlipNeg.x > 0.5);
            if (flipX) data.uvX.x = -data.uvX.x;

            bool flipY = isPosY ? (triFlipPos.y > 0.5) : (triFlipNeg.y > 0.5);
            if (flipY) data.uvY.x = -data.uvY.x;

            bool flipZ = isPosZ ? (triFlipPos.z > 0.5) : (triFlipNeg.z > 0.5);
            if (flipZ) data.uvZ.x = -data.uvZ.x;

            return data;
        }

        // --- 函数实现 ---

        vec4 SampleTriplanar(sampler2D theMap, TriplanarData data) {
            vec4 colX = texture(theMap, data.uvX);
            vec4 colY = texture(theMap, data.uvY);
            vec4 colZ = texture(theMap, data.uvZ);
            return colX * data.blend.x + colY * data.blend.y + colZ * data.blend.z;
        }

        vec3 SampleTriplanarNormal(sampler2D normMap, TriplanarData data, vec3 worldNormal) {
            // 1. 采样
            vec3 nX = texture(normMap, data.uvX).xyz;
            vec3 nY = texture(normMap, data.uvY).xyz;
            vec3 nZ = texture(normMap, data.uvZ).xyz;

            // 2. 解包 (0~1 -> -1~1) 并处理翻转
            if (flipNormalY) {
                nX.g = 1.0 - nX.g; nY.g = 1.0 - nY.g; nZ.g = 1.0 - nZ.g;
            }
            nX = nX * 2.0 - 1.0;
            nY = nY * 2.0 - 1.0;
            nZ = nZ * 2.0 - 1.0;

            // 应用强度
            nX.xy *= normalStrength; nY.xy *= normalStrength; nZ.xy *= normalStrength;
            // 重新归一化是必要的，因为修改强度后长度变了
            nX = normalize(nX); nY = normalize(nY); nZ = normalize(nZ);

            // 3. 转换到世界空间 (修正部分)
            // 这里的逻辑是：
            // 切线空间的 Z (n.z) 对应 世界空间的 几何法线方向 (X, Y 或 Z)
            // 切线空间的 X (n.x) 对应 切线 (Tangent)
            // 切线空间的 Y (n.y) 对应 副切线 (Bitangent)
            
            // X面投影 (ZY平面 UV): U->Z(Tangent), V->Y(Bitangent), Face->X
            // 修正：将 nX.z 放入 X 分量
            vec3 worldNormalX = vec3(nX.z, nX.y, nX.x); 

            // Y面投影 (XZ平面 UV): U->X(Tangent), V->Z(Bitangent), Face->Y
            // 修正：将 nY.z 放入 Y 分量
            vec3 worldNormalY = vec3(nY.x, nY.z, nY.y); 

            // Z面投影 (XY平面 UV): U->X(Tangent), V->Y(Bitangent), Face->Z
            // 修正：将 nZ.z 放入 Z 分量
            vec3 worldNormalZ = vec3(nZ.x, nZ.y, nZ.z);

            // 4. 处理背面翻转
            // 如果几何法线指向负方向，我们需要翻转重构出的法线的主轴分量
            // 这样才能保证法线贴图的 Z 依然指向“表面外侧”
            if (worldNormal.x < 0.0) worldNormalX.x = -worldNormalX.x;
            if (worldNormal.y < 0.0) worldNormalY.y = -worldNormalY.y;
            if (worldNormal.z < 0.0) worldNormalZ.z = -worldNormalZ.z;

            // 5. 混合
            vec3 blendedNormal = worldNormalX * data.blend.x + 
                                worldNormalY * data.blend.y + 
                                worldNormalZ * data.blend.z;
            
            return normalize(blendedNormal);
        }

        void CalcDirLight(DirLight light, vec3 N, vec3 V, vec3 albedo, vec3 F0, float shadow, inout vec3 diffAccum, inout vec3 specAccum) {
            vec3 L = normalize(-light.direction);
            vec3 H = normalize(V + L);
            float NdotL = max(dot(N, L), 0.0);
            
            // Radiance
            vec3 radiance = light.color * light.intensity * shadow;

            // Cook-Torrance
            float NDF = DistributionGGX(N, H, material.roughness);   
            float G   = GeometrySmith(N, V, L, material.roughness);      
            vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0);
           
            vec3 numerator    = NDF * G * F; 
            float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
            vec3 specular = numerator / denominator;
            
            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - material.metallic;	  

            // 累加
            diffAccum += (kD * albedo / PI) * radiance * NdotL;
            specAccum += specular * radiance * NdotL;
        }

        void CalcPointLight(PointLight light, vec3 N, vec3 pos, vec3 V, vec3 albedo, vec3 F0, float shadow, inout vec3 diffAccum, inout vec3 specAccum) {
            vec3 L = normalize(light.position - pos);
            vec3 H = normalize(V + L);
            float distance = length(light.position - pos);
            float attenuation = GetAttenuation(distance, light.range);
            float NdotL = max(dot(N, L), 0.0);

            vec3 radiance = light.color * light.intensity * attenuation * shadow;

            float NDF = DistributionGGX(N, H, material.roughness);   
            float G   = GeometrySmith(N, V, L, material.roughness);      
            vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0);
           
            vec3 numerator    = NDF * G * F; 
            float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
            vec3 specular = numerator / denominator;
            
            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - material.metallic;	  

            diffAccum += (kD * albedo / PI) * radiance * NdotL;
            specAccum += specular * radiance * NdotL;
        }

        void CalcSpotLight(SpotLight light, vec3 N, vec3 pos, vec3 V, vec3 albedo, vec3 F0, inout vec3 diffAccum, inout vec3 specAccum) {
            vec3 L = normalize(light.position - pos);
            vec3 H = normalize(V + L);
            float distance = length(light.position - pos);
            float attenuation = GetAttenuation(distance, light.range);
            float NdotL = max(dot(N, L), 0.0);

            float theta = dot(L, normalize(-light.direction)); 
            float epsilon = light.cutOff - light.outerCutOff;
            float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);

            vec3 radiance = light.color * light.intensity * attenuation * intensity;

            float NDF = DistributionGGX(N, H, material.roughness);   
            float G   = GeometrySmith(N, V, L, material.roughness);      
            vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0);
           
            vec3 numerator    = NDF * G * F; 
            float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
            vec3 specular = numerator / denominator;
            
            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - material.metallic;	  

            diffAccum += (kD * albedo / PI) * radiance * NdotL;
            specAccum += specular * radiance * NdotL;
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

    // 预设纹理槽位，防止采样器冲突
    _mainShader->use();
    _mainShader->setUniformInt("diffuseMap", 0);  // Slot 0: Albedo
    _mainShader->setUniformInt("normalMap", 1);   // Slot 1: Normal
    _mainShader->setUniformInt("shadowMap", 2);   // Slot 2: CSM Array
    _mainShader->setUniformInt("envMap", 3);      // Slot 3: IBL Cubemap (改到3，避开NormalMap)

    // Slot 4,5,6,7 用于点光源阴影
    int pointShadowSamplers[4] = {4, 5, 6, 7};
    glUniform1iv(glGetUniformLocation(_mainShader->getHandle(), "pointShadowMaps"), 4, pointShadowSamplers);

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
            if (mesh->isGizmo) mesh->material.albedo = light->color;
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
            if (mesh->isGizmo) mesh->material.albedo = light->color;
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

        if (meshComp->normalMap) {
            meshComp->normalMap->bind(1); // 绑定到 Slot 1
            _mainShader->setUniformBool("hasNormalMap", true);
            _mainShader->setUniformFloat("normalStrength", meshComp->normalStrength);
            _mainShader->setUniformBool("flipNormalY", meshComp->flipNormalY);
        } else {
            _mainShader->setUniformBool("hasNormalMap", false);
        }
        
        _mainShader->setUniformBool("isUnlit", meshComp->isGizmo);
        _mainShader->setUniformBool("isDoubleSided", meshComp->doubleSided);
        // _mainShader->setUniformVec3("material.ambient", meshComp->material.ambient);
        // _mainShader->setUniformVec3("material.diffuse", meshComp->material.diffuse);
        // _mainShader->setUniformVec3("material.specular", meshComp->material.specular);
        // _mainShader->setUniformFloat("material.shininess", meshComp->material.shininess);
        _mainShader->setUniformVec3("material.albedo", meshComp->material.albedo);
        _mainShader->setUniformFloat("material.metallic", meshComp->material.metallic);
        _mainShader->setUniformFloat("material.roughness", meshComp->material.roughness);
        _mainShader->setUniformFloat("material.ao", meshComp->material.ao);

        _mainShader->setUniformFloat("material.reflectivity", meshComp->material.reflectivity);
        _mainShader->setUniformFloat("material.refractionIndex", meshComp->material.refractionIndex);
        _mainShader->setUniformFloat("material.transparency", meshComp->material.transparency);

        _mainShader->setUniformBool("useTriplanar", meshComp->useTriplanar);
        _mainShader->setUniformFloat("triplanarScale", meshComp->triplanarScale);
        glm::vec3 flipPos(
            meshComp->triFlipPosX ? 1.0f : 0.0f,
            meshComp->triFlipPosY ? 1.0f : 0.0f,
            meshComp->triFlipPosZ ? 1.0f : 0.0f
        );
        _mainShader->setUniformVec3("triFlipPos", flipPos);
        glm::vec3 flipNeg(
            meshComp->triFlipNegX ? 1.0f : 0.0f,
            meshComp->triFlipNegY ? 1.0f : 0.0f,
            meshComp->triFlipNegZ ? 1.0f : 0.0f
        );
        _mainShader->setUniformVec3("triFlipNeg", flipNeg);
        glm::vec3 rotPos(meshComp->triRotPosX, meshComp->triRotPosY, meshComp->triRotPosZ);
        _mainShader->setUniformVec3("triRotPos", rotPos);
        glm::vec3 rotNeg(meshComp->triRotNegX, meshComp->triRotNegY, meshComp->triRotNegZ);
        _mainShader->setUniformVec3("triRotNeg", rotNeg);

        auto probe = go->getComponent<ReflectionProbeComponent>();
        if (probe && probe->textureID != 0) {
            // 如果这个物体本身就是一个反射探针，我们就使用它生成的 Environment Map
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_CUBE_MAP, probe->textureID);
            
            _mainShader->setUniformBool("hasEnvMap", true);
            
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

        // [新增] IBL 强度控制
        // 建议：如果你觉得场景太亮，这里可以传 0.5 或者 0.3
        // 暂时硬编码为 0.5 试试，或者后续在 Inspector 里加个 Scene Settings
        _mainShader->setUniformFloat("iblIntensity", 0.4f);

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