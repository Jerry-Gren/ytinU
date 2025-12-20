#include "shadow_map_pass.h"
#include <iostream>

ShadowMapPass::ShadowMapPass(int resolution, int maxLights) 
    : _resolution(resolution), _maxLights(maxLights)
{
    // 定义级联层级 (分割距离)
    // 实际层级数 = _cascadeLevels.size() + 1
    // 例如: [10, 50, 200, 800, 2000] -> 5个分割点 -> 6层级联
    _cascadeLevels = { 10.0f, 50.0f, 200.0f, 800.0f, 2000.0f };
    
    _layerCountPerLight = (int)_cascadeLevels.size() + 1;

    // 预分配矩阵空间: 灯光数 * 每灯层数
    _lightSpaceMatrices.resize(_maxLights * _layerCountPerLight);

    initShader();
    initFBO();
}

ShadowMapPass::~ShadowMapPass()
{
    if (_fbo) glDeleteFramebuffers(1, &_fbo);
    if (_depthMap) glDeleteTextures(1, &_depthMap);
}

void ShadowMapPass::initFBO()
{
    glGenFramebuffers(1, &_fbo);
    
    glGenTextures(1, &_depthMap);
    glBindTexture(GL_TEXTURE_2D_ARRAY, _depthMap); // 绑定为数组纹理

    // 分配 3D 内存
    // 总层数 = 最大灯光数 * 每个灯光的级联数
    int totalLayers = _maxLights * _layerCountPerLight;

    glTexImage3D(
        GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, 
        _resolution, _resolution, totalLayers, 
        0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL
    );
    
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    // 边界颜色设为 1.0 (最大深度，即无阴影)
    constexpr float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    
    // 将 Texture Array 的第 0 层附加到 FBO，但这只是暂时的
    // 在渲染循环中，我们会动态改变 attachment
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, _depthMap, 0, 0);
    
    // 我们不需要颜色缓冲，只记录深度
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::ShadowMapFBO:: Framebuffer is not complete!" << std::endl;
        
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMapPass::initShader()
{
    // 极简 Vertex Shader：把顶点变换到光空间
    const char* vsCode = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;

        uniform mat4 lightSpaceMatrix;
        uniform mat4 model;
        uniform float normalBias;

        void main() {
            // 1. 计算世界空间位置
            vec3 posWS = vec3(model * vec4(aPos, 1.0));
            
            // 2. 计算世界空间法线 (简化计算，假设没有非均匀缩放，或者在CPU传NormalMatrix)
            // 为了性能，且在ShadowPass，我们简单用 model 旋转部分
            vec3 normWS = normalize(mat3(model) * aNormal);

            // 3. [核心] 应用 Normal Bias
            // 沿着法线反方向向内收缩顶点
            // 加上光线方向的修正：只有当表面朝向光源时才需要收缩 (可选，Unity的做法比较复杂，这里用最简单的收缩)
            posWS -= normWS * normalBias;

            gl_Position = lightSpaceMatrix * vec4(posWS, 1.0);
        }
    )";

    // Empty Fragment Shader：不需要做任何事，深度写入是自动的
    const char* fsCode = R"(
        #version 330 core
        void main() {
            // gl_FragDepth = gl_FragCoord.z;
        }
    )";

    _depthShader.reset(new GLSLProgram);
    _depthShader->attachVertexShader(vsCode);
    _depthShader->attachFragmentShader(fsCode);
    _depthShader->link();
}

void ShadowMapPass::render(const Scene& scene, const std::vector<ShadowCasterInfo>& casters, Camera* camera)
{
    // 1. 重置矩阵列表
    // 注意：我们不 clear() 而是 resize，或者直接覆盖，保持大小一致
    // 如果实际灯光数少于 maxLights，后面的矩阵可以是单位矩阵或无效值
    int requiredSize = _maxLights * _layerCountPerLight;
    if (_lightSpaceMatrices.size() != requiredSize) {
        _lightSpaceMatrices.resize(requiredSize);
    }
    
    // 获取相机参数
    float camNear = 0.1f;
    float camFar = 1000.0f;
    if (auto pCam = dynamic_cast<PerspectiveCamera*>(camera)) {
        camNear = pCam->znear;
        camFar = pCam->zfar;
    } else if (auto oCam = dynamic_cast<OrthographicCamera*>(camera)) {
        camNear = oCam->znear;
        camFar = oCam->zfar;
    }

    // 2. 准备渲染
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    glViewport(0, 0, _resolution, _resolution);
    _depthShader->use();

    // 为了安全，先清除所有层（或者只清除用到的层）
    // 为了性能，我们可以在下面的循环中 glClear，但需要注意 GL 状态
    // 这里最简单的方式是清除整个 FBO，但 FBO 绑定的是 Layer，所以无法一次性清除 Array
    // 必须在循环里 Clear

    int lightCount = std::min((int)casters.size(), _maxLights);

    // --- 双重循环：遍历所有光源 ---
    for (int lightIdx = 0; lightIdx < lightCount; ++lightIdx)
    {
        const auto& caster = casters[lightIdx];

        // 设置当前光源的参数
        _depthShader->setUniformFloat("normalBias", caster.shadowNormalBias);
        glEnable(GL_CULL_FACE);
        glCullFace(caster.cullFaceMode);

        // --- 遍历级联 ---
        for (int cascadeIdx = 0; cascadeIdx < _layerCountPerLight; ++cascadeIdx)
        {
            float prevSplit = (cascadeIdx == 0) ? camNear : _cascadeLevels[cascadeIdx - 1];
            float currSplit = (cascadeIdx < _cascadeLevels.size()) ? _cascadeLevels[cascadeIdx] : camFar;

            // 1. 计算矩阵
            glm::mat4 matrix = getLightSpaceMatrix(prevSplit, currSplit, caster.direction, camera);
            
            // 2. 存储矩阵到扁平数组
            // 索引 = 光源Index * 每光层数 + 当前层
            int globalLayerIdx = lightIdx * _layerCountPerLight + cascadeIdx;
            _lightSpaceMatrices[globalLayerIdx] = matrix;

            // 3. 绑定 FBO 到对应的 Texture Layer
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, _depthMap, 0, globalLayerIdx);
            
            // 4. 清除当前层的深度缓冲
            glClear(GL_DEPTH_BUFFER_BIT);

            // 5. 提交矩阵并绘制
            _depthShader->setUniformMat4("lightSpaceMatrix", matrix);

            // 绘制场景
            for (const auto& go : scene.getGameObjects()) {
                auto meshComp = go->getComponent<MeshComponent>();
                if (!meshComp || !meshComp->enabled) continue;
                if (meshComp->isGizmo) continue;

                glm::mat4 model = go->transform.getLocalMatrix();
                // 叠加 model 自身的 local matrix (如果有)
                if (meshComp->model) {
                     model = model * meshComp->model->transform.getLocalMatrix();
                     _depthShader->setUniformMat4("model", model);
                     meshComp->model->draw();
                }
            }
        }
    }

    // 恢复状态
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::vector<glm::vec4> ShadowMapPass::getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view)
{
    const auto inv = glm::inverse(proj * view);
    
    std::vector<glm::vec4> frustumCorners;
    for (unsigned int x = 0; x < 2; ++x) {
        for (unsigned int y = 0; y < 2; ++y) {
            for (unsigned int z = 0; z < 2; ++z) {
                const glm::vec4 pt = inv * glm::vec4(
                    2.0f * x - 1.0f,
                    2.0f * y - 1.0f,
                    2.0f * z - 1.0f,
                    1.0f);
                frustumCorners.push_back(pt / pt.w);
            }
        }
    }
    return frustumCorners;
}

glm::mat4 ShadowMapPass::getLightSpaceMatrix(const float nearPlane, const float farPlane, const glm::vec3& lightDir, Camera* camera)
{
    // 1. 根据相机类型计算当前切片的投影矩阵
    glm::mat4 proj;
    if (auto pCam = dynamic_cast<PerspectiveCamera*>(camera)) {
        proj = glm::perspective(pCam->fovy, pCam->aspect, nearPlane, farPlane);
    } 
    else if (auto oCam = dynamic_cast<OrthographicCamera*>(camera)) {
        proj = glm::ortho(oCam->left, oCam->right, oCam->bottom, oCam->top, nearPlane, farPlane);
    }
    else {
        return glm::mat4(1.0f);
    }
    
    // 2. 获取该切片的世界空间 8 个角点
    auto corners = getFrustumCornersWorldSpace(proj, camera->getViewMatrix());

    // 3. 计算几何中心
    glm::vec3 center = glm::vec3(0, 0, 0);
    for (const auto& v : corners) {
        center += glm::vec3(v);
    }
    center /= corners.size();

    // 4. 构建光照视图矩阵
    // 注意：这里的位置其实不重要，重要的是方向。我们将位置定在中心逆光方向远处
    const auto lightView = glm::lookAt(center - lightDir, center, glm::vec3(0.0f, 1.0f, 0.0f));

    // 5. 计算 AABB
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    for (const auto& v : corners)
    {
        const auto trf = lightView * v;
        minX = std::min(minX, trf.x);
        maxX = std::max(maxX, trf.x);
        minY = std::min(minY, trf.y);
        maxY = std::max(maxY, trf.y);
        minZ = std::min(minZ, trf.z);
        maxZ = std::max(maxZ, trf.z);
    }

    // [修复] 扩展 Z 轴范围
    // 在 View Space 中，物体通常在 -Z 方向。
    // maxZ 是"最近"的点（数值最大，例如 -10），minZ 是"最远"的点（数值最小，例如 -100）。
    // glm::ortho 的 near/far 通常是正数距离。
    // 所以 distance_near = -maxZ, distance_far = -minZ。

    // 为了捕捉位于切片前方（light 和切片之间）的遮挡物（如树干投射阴影到地面），
    // 我们需要把 Near Plane 大幅向光源方向拉伸。
    // 我们也把 Far Plane 稍微推远一点以防万一。
    float zMult = 10.0f;
if (nearPlane > 50.0f) {
    // 如果是远处的层级 (黄色/蓝色)，不要扩展那么多，甚至不扩展
    zMult = 1.0f; 
}
    if (minZ < 0) minZ *= zMult; else minZ /= zMult;
    if (maxZ < 0) maxZ /= zMult; else maxZ *= zMult;
    
    // [修正逻辑]
    // 上面原来的代码逻辑有问题。让我们用更稳健的方式：
    // 我们把 Near/Far 设置得非常大，覆盖整个场景可能范围。
    // 因为是正交投影，Z 范围大不会导致精度严重下降（不像透视投影）。
    
    // 简单的方案：以 Slice 边界为基础，向前后各扩展 200 米
    float zNear = -maxZ; 
    float zFar  = -minZ;

    // 6. 纹素对齐 (Texel Snapping) - 解决闪烁
    float unitPerPixel = (maxX - minX) / _resolution;
    float offsetX = fmod(minX, unitPerPixel);
    float offsetY = fmod(minY, unitPerPixel);
    minX -= offsetX;
    maxX -= offsetX;
    minY -= offsetY;
    maxY -= offsetY;

    float padding = 5.0f; 
    minX -= padding; maxX += padding;
    minY -= padding; maxY += padding;
    
    // 7. 构建正交投影
    // 注意：glm::ortho(l, r, b, t, zNear, zFar)
    const auto lightProjection = glm::ortho(minX, maxX, minY, maxY, zNear, zFar);

    return lightProjection * lightView;
}