#include "shadow_map_pass.h"
#include <iostream>

ShadowMapPass::ShadowMapPass(int resolution) 
    : _resolution(resolution)
{
    // 定义级联层级 (分割距离)
    // 这里我们定义 4 层级联。单位是米。
    // 能够覆盖近处细节 (0-10m) 到中远距离。
    // 注意：最后一层不需要存，它就是相机的 zFar。
    _cascadeLevels = { 10.0f, 50.0f, 200.0f, 800.0f, 2000.0f };
    // 实际层级是: [0, 10], [10, 50], [50, 200], [200, 800], [800, 2000], [2000, zFar] -> 共 6 层

    // 预分配矩阵空间
    _lightSpaceMatrices.resize(_cascadeLevels.size() + 1);

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
    // width, height, depth (层数)。我们有 4 层。
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, 
        _resolution, _resolution, int(_cascadeLevels.size() + 1), 
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
        layout (location = 1) in vec3 aNormal; // [新增] 我们需要法线

        uniform mat4 lightSpaceMatrix;
        uniform mat4 model;
        uniform float normalBias; // [新增]

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

void ShadowMapPass::render(const Scene& scene, const glm::vec3& lightDir, Camera* camera, float shadowNormalBias, unsigned int cullFaceMode)
{
    // 1. 计算每一层的矩阵
    _lightSpaceMatrices.clear();
    
    // 这里的 near/far 必须和相机的设置一致
    float camNear = 0.1f;
    float camFar = 1000.0f;

    if (auto pCam = dynamic_cast<PerspectiveCamera*>(camera)) {
        camNear = pCam->znear;
        camFar = pCam->zfar;
    } else if (auto oCam = dynamic_cast<OrthographicCamera*>(camera)) {
        camNear = oCam->znear;
        camFar = oCam->zfar;
    }

    // 循环处理每一层
    // 层级示例: 
    // 0: [near, 10]
    // 1: [10, 60]
    // 2: [60, 250]
    // 3: [250, far]
    for (size_t i = 0; i < _cascadeLevels.size() + 1; ++i)
    {
        float prevSplit = (i == 0) ? camNear : _cascadeLevels[i - 1];
        float currSplit = (i < _cascadeLevels.size()) ? _cascadeLevels[i] : camFar;

        _lightSpaceMatrices.push_back(getLightSpaceMatrix(prevSplit, currSplit, lightDir, camera));
    }

    // 2. 渲染流程
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    glViewport(0, 0, _resolution, _resolution);
    
    // 这里需要把每一层都清空，或者每次绘制前清空
    glClear(GL_DEPTH_BUFFER_BIT); 
    // 注意：TextureArray 的 clear 比较特殊，如果不放心，可以在下面循环里 clear，但那样效率略低。
    // 正确的做法是 glClear 会清除当前绑定的 Attachment。如果当前绑定的是 Layer 0，它只清 Layer 0 吗？
    // OpenGL 规范比较绕。为了安全起见，我们在循环里做 glClear。

    _depthShader->use();
    // 传递 Normal Bias (如果需要)
    _depthShader->setUniformFloat("normalBias", shadowNormalBias);

    // 开启剔除 (GL_BACK) 
    glEnable(GL_CULL_FACE);
    glCullFace(cullFaceMode);

    for (size_t i = 0; i < _lightSpaceMatrices.size(); ++i)
    {
        // [关键] 绑定当前层
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, _depthMap, 0, i);
        
        // 清除这一层的深度
        glClear(GL_DEPTH_BUFFER_BIT);

        // 设置当前层的矩阵
        _depthShader->setUniformMat4("lightSpaceMatrix", _lightSpaceMatrices[i]);

        // 绘制场景
        for (const auto& go : scene.getGameObjects()) {
            auto meshComp = go->getComponent<MeshComponent>();
            if (!meshComp || !meshComp->enabled) continue;
            if (meshComp->isGizmo) continue;

            glm::mat4 model = go->transform.getLocalMatrix();
            model = model * meshComp->model->transform.getLocalMatrix();
            _depthShader->setUniformMat4("model", model);

            meshComp->model->draw();
        }
    }

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