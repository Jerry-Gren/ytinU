#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <glad/gl.h>

#include "scene.h"
#include "base/camera.h"
#include "base/glsl_program.h"
#include "outline_pass.h"
#include "geometry_factory.h"
#include "shadow_map_pass.h"
#include "point_shadow_pass.h"
#include "planar_reflection_pass.h"

struct IBLProfile {
    GLuint envMap = 0;       // 天空盒
    GLuint irradianceMap = 0; // 漫反射积分
    GLuint prefilterMap = 0;  // 镜面反射预滤波
    
    bool isBaked = false;     // 标记是否已经烘焙过数据
};

class Renderer
{
public:
    Renderer();
    ~Renderer();

    // 初始化 Shader、天空盒、网格等资源
    void init();

    // 调整大小 (通知 OutlinePass 等)
    void onResize(int width, int height);

    // 加载 HDR 天空盒
    void loadSkyboxHDR(const std::string& path);

    // 将程序自动默认的天空盒画到_envCubemap上
    void updateProceduralSkybox(const SceneEnvironment& env);

    // 渲染指定的物体列表 (通用函数)
    void renderObjectList(const std::vector<GameObject*>& objects, 
                          const Scene& scene, 
                          const GameObject* excludeObject = nullptr,
                          const ReflectionProbeComponent* activeProbe = nullptr,
                          const GameObject* activeProbeObj = nullptr,
                          const Frustum* frustum = nullptr);
    
    void drawSkybox(const glm::mat4& view, const glm::mat4& proj, const SceneEnvironment& env);

    // 核心渲染函数
    // targetFBO: 传入 0 渲染到屏幕，传入 FBO ID 渲染到纹理
    // selectedObj: 如果非空，则绘制描边 (Editor 模式用)
    void render(const Scene& scene, Camera* camera, 
                GLuint targetFBO, int width, int height,
                float contentScale, 
                GameObject* selectedObj = nullptr);

    GLSLProgram* getMainShader() const { return _mainShader.get(); }

    // 定义反射纹理专用的纹理槽位 (Slot 18)
    // 0-6: 基础材质, 7-10: 点光源阴影, 11-13: IBL, 14-16: ORM独立, 17: 背面深度
    static constexpr int PLANAR_REFLECTION_SLOT = 18;

private:
    // --- Shader 资源 ---
    std::unique_ptr<GLSLProgram> _mainShader;
    std::unique_ptr<GLSLProgram> _gridShader;
    std::unique_ptr<GLSLProgram> _skyboxShader;
    std::unique_ptr<GLSLProgram> _equirectangularToCubemapShader;
    std::unique_ptr<GLSLProgram> _irradianceShader;
    std::unique_ptr<GLSLProgram> _prefilterShader;
    std::unique_ptr<GLSLProgram> _brdfShader;
    
    std::unique_ptr<ShadowMapPass> _shadowPass;
    std::unique_ptr<PointShadowPass> _pointShadowPass;
    std::unique_ptr<PlanarReflectionPass> _planarReflectionPass;

    // --- 全局模型 ---
    std::shared_ptr<Model> _gridPlane;
    std::shared_ptr<Model> _skyboxCube;

    // --- Render Pass ---
    std::unique_ptr<OutlinePass> _outlinePass;

    IBLProfile _resProcedural; // 程序化专用
    IBLProfile _resHDR;        // HDR 专用
    void allocateIBLTextures(IBLProfile& profile);
    void computeIrradianceMap(const IBLProfile& profile);
    void computePrefilterMap(const IBLProfile& profile);

    GLuint _brdfLUT = 0; // BRDF 查找表

    // 用于玻璃折射的背景纹理 (Grab Pass Texture)
    GLuint _sceneColorMap = 0;

    // 深度抓取相关
    GLuint _sceneDepthMap = 0;
    GLuint _grabFbo = 0; // 用于辅助深度拷贝的 FBO 容器

    // 背面深度 Pass 相关资源
    GLuint _sceneBackfaceDepthMap = 0;
    GLuint _backfaceFbo = 0;

    // --- 内部绘制函数 ---
    void initSkyboxResources(); // 初始化转换用的 Shader 和 空纹理
    void initIBLResources(); // 初始化 IBL 相关的 Shader 和 Texture
    void initPrefilterResources();
    void initBRDFResources();
    void computeBRDFLUT();
    void initSceneColorMap(int width, int height);
    void initSceneDepthMap(int width, int height);
    void initBackfaceDepthMap(int width, int height);
    void drawGrid(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& viewPos);
    // 设置 Shader 的全局光照参数 (灯光、阴影、环境贴图)
    void setupShaderLighting(const Scene& scene, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& viewPos, 
                             const std::vector<LightComponent*>& dirLights,
                             const std::vector<LightComponent*>& pointLights,
                             const std::vector<LightComponent*>& spotLights,
                             const std::unordered_map<LightComponent*, int>& shadowIndices);
    
    // 渲染物体背面
    void renderBackfacePass(const std::vector<GameObject*>& objects, const Frustum* frustum);
    // 更新场景中的所有反射探针
    void updateReflectionProbes(const Scene& scene);
};