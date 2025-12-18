#include "scene_view_panel.h"
#include <imgui.h>
#include <iostream>
#include <limits> // for std::numeric_limits

SceneViewPanel::SceneViewPanel() : Panel("3D Viewport")
{
    // 初始化 FBO (初始大小可以为 0，后面会自动 Resize)
    initFBO(100, 100); 
    
    // 初始化相机控制器
    // 初始宽高暂定为 800x600，会在第一次渲染时修正
    _cameraController = std::make_unique<EditorCamera>(800, 600);
}

SceneViewPanel::~SceneViewPanel()
{
    if (_fbo.id) glDeleteFramebuffers(1, &_fbo.id);
    if (_fbo.texture) glDeleteTextures(1, &_fbo.texture);
    if (_fbo.rbo) glDeleteRenderbuffers(1, &_fbo.rbo);
}

void SceneViewPanel::initFBO(int width, int height)
{
    _fbo.width = width;
    _fbo.height = height;

    glGenFramebuffers(1, &_fbo.id);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo.id);

    glGenTextures(1, &_fbo.texture);
    glBindTexture(GL_TEXTURE_2D, _fbo.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _fbo.texture, 0);

    glGenRenderbuffers(1, &_fbo.rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, _fbo.rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _fbo.rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: SceneView Framebuffer is not complete!" << std::endl;
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneViewPanel::resizeFBO(int width, int height)
{
    if (_fbo.width == width && _fbo.height == height) return;
    
    glDeleteFramebuffers(1, &_fbo.id);
    glDeleteTextures(1, &_fbo.texture);
    glDeleteRenderbuffers(1, &_fbo.rbo);
    initFBO(width, height);
}
// [修改后的签名]
void SceneViewPanel::onImGuiRender(Scene* scene, Renderer* renderer, GameObject*& selectedObject, float contentScale)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f)); 
    if (!ImGui::Begin(_title.c_str(), &_isOpen)) {
        ImGui::End(); ImGui::PopStyleVar(); return;
    }

    _isFocused = ImGui::IsWindowFocused(); 
    _isHovered = ImGui::IsWindowHovered();

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    if (viewportPanelSize.x <= 0) viewportPanelSize.x = 1;
    if (viewportPanelSize.y <= 0) viewportPanelSize.y = 1;

    int rawWidth = (int)(viewportPanelSize.x * contentScale);
    int rawHeight = (int)(viewportPanelSize.y * contentScale);

    // 2. Resize FBO
    if (rawWidth != _fbo.width || rawHeight != _fbo.height)
    {
        resizeFBO(rawWidth, rawHeight);
        renderer->onResize(rawWidth, rawHeight);
        _cameraController->onResize(rawWidth, rawHeight);
    }

    // 3. 执行渲染 (Render to FBO)
    // 注意：这里我们直接调用 renderer，不再需要 SceneRoaming 中转
    if (_fbo.id != 0) {
        renderer->render(*scene, 
                         _cameraController->getActiveCamera(), 
                         _fbo.id, rawWidth, rawHeight, 
                         contentScale, selectedObject);
    }

    // 4. 绘制 Image
    ImGui::Image((ImTextureID)(intptr_t)_fbo.texture, viewportPanelSize, ImVec2(0, 1), ImVec2(1, 0));

    // 记录视口位置 (用于射线检测和 Gizmo)
    _viewportPos = ImGui::GetItemRectMin();
    _viewportSize = ImGui::GetItemRectSize();

    // 5. 绘制 Gizmo
    _cameraController->drawViewGizmo(
        glm::vec2(_viewportPos.x, _viewportPos.y), 
        glm::vec2(_viewportSize.x, _viewportSize.y)
    );

    ImGui::End();
    ImGui::PopStyleVar();
}

// [完整签名]
void SceneViewPanel::onInputUpdate(float dt, Scene* scene, GameObject*& selectedObject)
{
    // 如果键盘正被 UI 占用（例如正在输入文字），不处理 3D 快捷键
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    _isControlling = _cameraController->isControlling();

    // 快捷键 (F 聚焦)
    // 这里需要 GLFW 窗口句柄来检测按键吗？ ImGui 提供了 IsKeyPressed
    if (ImGui::IsKeyPressed(ImGuiKey_F)) {
        _cameraController->frameObject(selectedObject);
    }
    
    // Key 1: Front View (+Z)
    if (ImGui::IsKeyPressed(ImGuiKey_1) || ImGui::IsKeyPressed(ImGuiKey_Keypad1)) {
        _cameraController->switchToView(glm::vec3(0, 0, 1));
    }
    // Key 3: Right View (+X)
    if (ImGui::IsKeyPressed(ImGuiKey_3) || ImGui::IsKeyPressed(ImGuiKey_Keypad3)) {
        _cameraController->switchToView(glm::vec3(1, 0, 0));
    }
    // Key 7: Top View (+Y)
    if (ImGui::IsKeyPressed(ImGuiKey_7) || ImGui::IsKeyPressed(ImGuiKey_Keypad7)) {
        _cameraController->switchToView(glm::vec3(0, 1, 0));
    }

    // Delete
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedObject)
    {
        if (scene) scene->markForDestruction(selectedObject);
        selectedObject = nullptr; 
    }

    if (_isHovered || _isFocused || _isControlling) {
        _cameraController->handleInput(); 
    }
    _cameraController->update(dt);

    // 拾取逻辑
    if (_isHovered && ImGui::IsMouseClicked(0) && !ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
        // 还要检查是否点到了 Gizmo (isControlling)
        if (!_cameraController->isControlling()) {
            handleMousePick(scene, selectedObject);
        }
    }
}

// [搬运] handleMousePick
void SceneViewPanel::handleMousePick(Scene* scene, GameObject*& selectedObject)
{
    // [变化1] 使用成员变量 _isHovered
    if (!_isHovered) 
        return;

    // [变化2] 使用 ImGui 获取鼠标绝对坐标 (替代 glfwGetCursorPos)
    ImVec2 mousePos = ImGui::GetMousePos();
    float mouseX = mousePos.x;
    float mouseY = mousePos.y;

    // [变化3] 使用成员变量 _cameraController, _viewportPos, _viewportSize
    auto camRay = _cameraController->screenPointToRay(
        mouseX, mouseY, 
        _viewportPos.x, _viewportPos.y, 
        _viewportSize.x, _viewportSize.y
    );

    // 转换成 Physics Ray
    // (假设 EditorCamera::Ray 和 PhysicsUtils::Ray 结构一致，或者是同一种类型)
    Ray worldRay(camRay.origin, camRay.direction);

    // [调试]
    // std::cout << "Ray Dir: " << worldRay.direction.x << ", " 
    //           << worldRay.direction.y << ", " << worldRay.direction.z << std::endl;

    GameObject *closestObj = nullptr;
    float closestDist = std::numeric_limits<float>::max();

    // [变化4] 使用传入的 scene 指针
    if (scene) 
    {
        const auto& objects = scene->getGameObjects();
        for (const auto &go : objects)
        {
            auto meshComp = go->getComponent<MeshComponent>();
            if (!meshComp || !meshComp->enabled) continue;

            // --- 以下数学逻辑完全保持不变 ---

            // 1. 计算 Model Matrix
            glm::mat4 modelMatrix = go->transform.getLocalMatrix();
            modelMatrix = modelMatrix * meshComp->model->transform.getLocalMatrix();

            // 2. 将射线转到局部空间
            glm::mat4 invModel = glm::inverse(modelMatrix);

            glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(worldRay.origin, 1.0f));
            glm::vec3 localDir = glm::vec3(invModel * glm::vec4(worldRay.direction, 0.0f));
            
            // 归一化
            localDir = glm::normalize(localDir);

            Ray localRay(localOrigin, localDir);

            // 3. 检测
            float tBox = 0.0f;
            if (PhysicsUtils::intersectRayAABB(localRay, meshComp->model->getBoundingBox(), tBox))
            {
                // 如果只击中盒子，还不算选中，必须击中三角形
                // 只有当 AABB 击中时，才进行昂贵的 Mesh 检测
                
                // ==================================================
                // Phase 2: 精测 (Narrow Phase) - Mesh
                // ==================================================
                float tMesh = 0.0f;
                const auto& verts = meshComp->model->getVertices();
                const auto& indices = meshComp->model->getIndices();

                if (PhysicsUtils::intersectRayMesh(localRay, verts, indices, tMesh))
                {
                    // [关键] tMesh 是局部空间的距离。
                    // 为了在不同缩放的物体之间正确排序，我们需要把它转换回世界空间距离。
                    // WorldPos = WorldOrigin + WorldDir * tWorld
                    // LocalPos = LocalOrigin + LocalDir * tMesh
                    // 简单的近似：把 LocalHitPos 转回 WorldPos，然后算距离。
                    
                    glm::vec3 localHitPos = localRay.origin + localRay.direction * tMesh;
                    glm::vec3 worldHitPos = glm::vec3(modelMatrix * glm::vec4(localHitPos, 1.0f));
                    float worldDist = glm::distance(worldRay.origin, worldHitPos);

                    if (worldDist < closestDist)
                    {
                        closestDist = worldDist;
                        closestObj = go.get();
                    }
                }
            }
        }
    }

    // [变化5] 更新传入的引用引用
    selectedObject = closestObj;
    
    // [调试]
    if(selectedObject) std::cout << "Picked: " << selectedObject->name << std::endl;
}