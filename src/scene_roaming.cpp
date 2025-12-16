#include "imgui.h"
#include "imgui_internal.h"
#include "scene_roaming.h"
#include "geometry_factory.h"
#include "ImGuiFileDialog.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/easing.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <filesystem>
#include <algorithm> // for std::sort

// 辅助结构：用于排序轴的绘制顺序
struct GizmoAxisData {
    glm::vec3 dir;       // 原始方向
    ImU32 mainColor;     // 主颜色 (外圈或实心)
    ImU32 fillColor;     // 填充颜色 (仅负轴使用，稍淡)
    char label;          // 标签文字 ('X', 'Y', 'Z' 或 0)
    bool isNegative;     // 是否是负轴
    float zDepth;        // 变换后的深度 (用于排序)
    ImVec2 screenPos;    // 变换后的屏幕位置
};

SceneRoaming::SceneRoaming(const Options &options) : Application(options)
{
    updateContentScale();

    // init cameras
    _cameras.resize(2);

    const float aspect = 1.0f * _windowWidth / _windowHeight;
    constexpr float znear = 0.1f;
    constexpr float zfar = 10000.0f;

    // perspective camera
    _cameras[0].reset(new PerspectiveCamera(glm::radians(60.0f), aspect, 0.1f, 10000.0f));
    // 初始状态：看着原点，距离 15 米
    _cameraPivot = glm::vec3(0.0f, 0.5f, 0.0f);
    _currentOrbitDist = 15.0f;
    _targetOrbitDist = 15.0f;
    _cameras[0]->transform.position = _cameraPivot + glm::vec3(0.0f, 0.0f, 1.0f) * _currentOrbitDist;
    
    // 稍微抬高一点角度
    glm::vec3 startPos = glm::vec3(0.0f, 5.0f, 15.0f);
    _currentOrbitDist = glm::length(startPos - _cameraPivot);
    _targetOrbitDist = _currentOrbitDist;
    _cameras[0]->transform.position = startPos;

    glm::mat4 view = glm::lookAt(_cameras[0]->transform.position, _cameraPivot, glm::vec3(0,1,0));
    _cameras[0]->transform.rotation = glm::quat_cast(glm::inverse(view));

    // orthographic camera
    _cameras[1].reset(
        new OrthographicCamera(-4.0f * aspect, 4.0f * aspect, -4.0f, 4.0f, znear, zfar));
    _cameras[1]->transform.position = glm::vec3(0.0f, 0.0f, 15.0f);

    glfwSetInputMode(_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    // 标记项目未打开
    _isProjectOpen = false;

    // =================================================
    // [新逻辑] 初始化子系统
    // =================================================
    _scene = std::make_unique<Scene>();
    _renderer = std::make_unique<Renderer>();

    // 1. 初始化渲染资源 (Shader, Skybox 等)
    _renderer->init();

    // 2. 初始化场景数据 (创建默认灯光等)
    _scene->createDefaultScene();

    initImGui();
    // initSceneFBO 不需要在这里调，第一次 renderUI 时会根据窗口大小自动调
}

SceneRoaming::~SceneRoaming()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void SceneRoaming::initSceneFBO(int width, int height)
{
    _sceneFbo.width = width;
    _sceneFbo.height = height;

    glGenFramebuffers(1, &_sceneFbo.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _sceneFbo.fbo);

    // 颜色附件 (Scene Texture)
    glGenTextures(1, &_sceneFbo.texture);
    glBindTexture(GL_TEXTURE_2D, _sceneFbo.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _sceneFbo.texture, 0);

    // 深度附件
    glGenRenderbuffers(1, &_sceneFbo.rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, _sceneFbo.rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _sceneFbo.rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneRoaming::resizeSceneFBO(int width, int height)
{
    if (_sceneFbo.width == width && _sceneFbo.height == height) return;
    
    // 简单粗暴：删了重建 (或者你可以用 glTexImage2D 重新分配内存)
    glDeleteFramebuffers(1, &_sceneFbo.fbo);
    glDeleteTextures(1, &_sceneFbo.texture);
    glDeleteRenderbuffers(1, &_sceneFbo.rbo);
    initSceneFBO(width, height);
}

void SceneRoaming::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark(); // 使用暗色主题

    ImGui_ImplGlfw_InitForOpenGL(_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // --- High DPI 适配逻辑 ---

    // 1. 缩放 UI 样式 (按钮大小、间距等)
    if (_contentScale > 1.0f)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(_contentScale);
    }

    // 2. 缩放字体
    // ImGui 默认字体是位图字体，直接缩放会模糊。
    // 强烈建议加载一个 TTF 字体并指定像素大小。
    // Windows 路径示例 (你可以换成你的项目内的字体路径 "media/fonts/arial.ttf")
    std::string fontPath = getAssetFullPath("fonts/Roboto-Regular.ttf");
    // 如果没有字体文件，可以用 Windows 自带的，或者暂时忽略字体清晰度
    // fontPath = "C:\\Windows\\Fonts\\segoeui.ttf"; 
    
    float fontSize = 16.0f * _contentScale; // 基础字号 16

    if (std::filesystem::exists(fontPath)) 
    {
        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize);
    }
    else
    {
        // 如果找不到字体，使用默认字体并缩放 (可能会模糊)
        io.FontGlobalScale = _contentScale;
    }
}

void SceneRoaming::updateContentScale()
{
    float x, y;
    glfwGetWindowContentScale(_window, &x, &y);
    // 取较大的那个作为主缩放比例
    _contentScale = (x > y) ? x : y;
}

void SceneRoaming::renderFrame()
{
    int currentW, currentH;
    glfwGetFramebufferSize(_window, &currentW, &currentH);

    // 如果长或宽为0（最小化状态），什么都不做，直接返回
    if (currentW == 0 || currentH == 0) {
        // 稍微休眠一下，避免空转占用 CPU 100%
        // (在 Windows 上可以使用 std::this_thread::sleep_for，或者简单的 return)
        return;
    }

    updateContentScale();

    // [新增] 更新平滑逻辑 (必须在 renderUI 之前)
    // 这样能保证每帧画面都是平滑过渡的
    updateSmoothZoom();
    updateCameraAnimation();

    // =========================================================
    // 2. 开启 ImGui 新帧 (必须在所有逻辑之前)
    // =========================================================
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    handleInput();

    // =========================================================
    // 3. 清理主屏幕 (Back Buffer)
    // =========================================================
    // 注意：这里的 Viewport 是整个窗口的大小，不是 FBO 的大小
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, currentW, currentH);
    // 清除为黑色 (ImGui 窗口背后的颜色)
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // =========================================================
    // 4. 执行 UI 逻辑 (并在内部触发 3D 渲染)
    // =========================================================
    // renderUI 会计算布局 -> 调整 FBO -> renderScene -> 提交 ImGui::Image
    renderUI();

    // =========================================================
    // 5. 提交 ImGui 绘制数据
    // =========================================================
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void SceneRoaming::renderScene()
{
    // 1. 安全检查
    if (_sceneFbo.fbo == 0) return; 

    // 2. 通知 Renderer 窗口可能变了 (用于 Resize OutlinePass 等)
    _renderer->onResize(_sceneFbo.width, _sceneFbo.height);

    // 3. [核心] 将渲染任务委托给 Renderer
    // 参数：场景数据、当前相机、目标FBO、宽高、当前选中物体(用于描边)
    _renderer->render(*_scene, 
                      _cameras[activeCameraIndex].get(), 
                      _sceneFbo.fbo, 
                      _sceneFbo.width, 
                      _sceneFbo.height, 
                      _selectedObject);
}

void SceneRoaming::renderUI()
{
    if (!_isProjectOpen)
    {
        renderProjectSelector(); // 阻塞式界面
        ImVec2 maxSize = ImGui::GetIO().DisplaySize;
        ImVec2 minSize = ImVec2(maxSize.x * 0.5f, maxSize.y * 0.5f);

        if (ImGuiFileDialog::Instance()->Display("ChooseDirDlgKey", ImGuiWindowFlags_NoCollapse, minSize, maxSize))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                std::string filePathName = ImGuiFileDialog::Instance()->GetCurrentPath();
                if (filePathName.length() < sizeof(_projectPathBuf)) {
                    strcpy(_projectPathBuf, filePathName.c_str());
                }
            }
            ImGuiFileDialog::Instance()->Close();
        }
    } else {
        // =======================================================
        // [核心] 真正的布局重置逻辑 (DockBuilder)
        // =======================================================
        
        // 获取主视口 ID
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");

        // 如果需要重置，或者第一次运行且没有 ini 记录
        // (ImGui::DockBuilderGetNode 判断该 ID 是否已存在)
        if (!_isLayoutInitialized || (ImGui::DockBuilderGetNode(dockspace_id) == NULL))
        {
            // 1. 清除当前所有布局
            ImGui::DockBuilderRemoveNode(dockspace_id); 
            
            // 2. 添加一个空的根节点，覆盖整个视口
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

            // 3. [关键] 切分节点 (Split)
            // 类似于切蛋糕：先切一刀，再在剩下的部分切一刀
            
            ImGuiID dock_main_id = dockspace_id; // 初始 ID
            ImGuiID dock_right_id = 0;
            ImGuiID dock_left_id = 0;
            ImGuiID dock_bottom_id = 0;

            // 第一刀：把右边切出来 (占 20%) -> 放 Inspector
            dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.2f, nullptr, &dock_main_id);
            
            // 第二刀：把左边切出来 (占 20%) -> 放 Hierarchy
            dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
            
            // 第三刀：把下面切出来 (占 25%) -> 放 Project
            dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);
            
            // 剩下的 dock_main_id 就是中间的部分 -> 放 3D Viewport

            // 4. 将窗口绑定到对应的 ID
            // 注意：这里的字符串必须和你 Begin() 里的名字完全一致！
            ImGui::DockBuilderDockWindow("3D Viewport", dock_main_id);
            ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left_id);
            ImGui::DockBuilderDockWindow("Inspector", dock_right_id);
            ImGui::DockBuilderDockWindow("Project / Assets", dock_bottom_id);

            // 5. 完成构建
            ImGui::DockBuilderFinish(dockspace_id);

            _isLayoutInitialized = true;
        }

        // =======================================================
        // 正常渲染
        // =======================================================

        // 绑定到我们刚才构建的 ID
        ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport());

        // =======================================================
        // 2. 顶部菜单栏 (可选，模仿 Blender)
        // =======================================================
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(_window, true);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Reset Layout")) _isLayoutInitialized = false;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // =======================================================
        // 3. 3D 视口窗口 (最重要的部分)
        // =======================================================
        // 去掉窗口内边距，让纹理填满整个窗口，看起来像原生应用
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f)); 
        ImGui::Begin("3D Viewport");

        _isViewportFocused = ImGui::IsWindowFocused(); 
        _isViewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

        // 获取当前窗口可用的内容区域大小
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        
        // 防止无效大小导致崩溃
        if (viewportPanelSize.x <= 0) viewportPanelSize.x = 1;
        if (viewportPanelSize.y <= 0) viewportPanelSize.y = 1;

        // 如果大小发生了变化（用户拖拽了窗口），通知 FBO 重建
        // 注意：这里可能需要处理一下最小尺寸，防止缩小到 0 崩溃
        if ((int)viewportPanelSize.x != _sceneFbo.width || (int)viewportPanelSize.y != _sceneFbo.height)
        {
            resizeSceneFBO((int)viewportPanelSize.x, (int)viewportPanelSize.y);
            
            // 更新相机的宽高比
            float newAspect = viewportPanelSize.x / viewportPanelSize.y;
            if (auto pCam = dynamic_cast<PerspectiveCamera *>(_cameras[activeCameraIndex].get())) {
                pCam->aspect = newAspect;
            }
        }

        // 2. [核心修复] 在绘制 Image 之前，立即渲染场景！
        // 这保证了 _sceneFbo.texture 里永远是最新的、尺寸匹配的画面
        renderScene();

        // [关键] 将 FBO 纹理画在 ImGui 窗口上
        // OpenGL 纹理坐标原点在左下，ImGui 在左上，所以 UV 需要翻转: (0,1) -> (1,0)
        ImGui::Image(
            (ImTextureID)(intptr_t)_sceneFbo.texture, 
            viewportPanelSize, 
            ImVec2(0, 1), 
            ImVec2(1, 0)
        );

        // [关键] 记录图片的绝对坐标和大小，供 handleMousePick 使用
        // GetItemRectMin 返回的是刚刚绘制的 Item (也就是 Image) 的左上角屏幕坐标
        _viewportPos = ImGui::GetItemRectMin();
        _viewportSize = ImGui::GetItemRectSize();
        // _isViewportHovered = ImGui::IsItemHovered(); // 只有鼠标在图片上时为 true

        // =========================================================
        // [新增] 绘制 View Gizmo (右上角)
        // =========================================================
        {
            // [变大] 轴长度：从 45 改为 55
            float gizmoSize = 65.0f; 
            
            // [核心修复] 动态计算 Padding 防止溢出
            // Padding = 轴长 + 圆圈半径(15) + 安全边距(30)
            // 这样保证即使轴指向 45 度角，圆圈也绝不会切出屏幕
            float safePadding = gizmoSize + 15.0f + 30.0f;

            ImVec2 gizmoCenter = ImVec2(
                _viewportPos.x + _viewportSize.x - safePadding,
                _viewportPos.y + safePadding
            );

            glm::mat4 view = _cameras[activeCameraIndex]->getViewMatrix();

            // [新增] 用于接收 Gizmo 是否被 Hover
            bool isGizmoHovered = false;

            glm::vec3 clickedDir = drawViewGizmo(
                ImGui::GetWindowDrawList(), 
                _cameras[activeCameraIndex]->transform.position, 
                view, 
                gizmoCenter, 
                gizmoSize,
                isGizmoHovered
            );

            // ====================================================
            // [新增] Gizmo 拖拽旋转逻辑
            // ====================================================
            
            // 1. 如果鼠标点击了 Gizmo 区域，但没有点中具体的轴 -> 开始拖拽
            // 注意：glm::length(clickedDir) > 0.1f 表示点中了具体的轴
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && isGizmoHovered && glm::length(clickedDir) < 0.1f)
            {
                _isGizmoDragging = true;
            }

            // 2. 如果松开鼠标 -> 停止拖拽
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                _isGizmoDragging = false;
            }

            // 3. 执行拖拽旋转
            if (_isGizmoDragging)
            {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                
                // 灵敏度调整：Blender 的 Gizmo 旋转类似于 "轨迹球"
                // 向右拖动 = 视角向右转 = 摄像机向左绕
                float sens = 0.005f; 
                rotateCamera(-delta.x * sens, -delta.y * sens);
            }

            // ====================================================
            // 轴点击逻辑 (Snap View)
            // ====================================================
            // 只有在没拖拽的时候才响应轴点击
            if (!_isGizmoDragging && glm::length(clickedDir) > 0.1f)
            {
                // 1. 基于 Pivot 计算目标位置
                float dist = glm::length(_cameras[activeCameraIndex]->transform.position - _cameraPivot);
                if (dist < 1.0f) dist = 5.0f;

                glm::vec3 targetPos = _cameraPivot + clickedDir * dist;

                // 2. [核心修复] 智能 Up 向量计算
                glm::vec3 up = glm::vec3(0, 1, 0); 

                glm::vec3 currentDir = glm::normalize(_cameras[activeCameraIndex]->transform.position - _cameraPivot);
                glm::vec3 currentUp = _cameras[activeCameraIndex]->transform.getUp();

                // --- 情况 A: 点击的是 Top/Bottom (Y轴) ---
                if (abs(clickedDir.y) > 0.9f) {
                    // [新增] 检查当前是否"倒立" (Up.y < 0)
                    // 如果倒立，我们需要反转逻辑，以维持 360 度翻转的连续性
                    float invert = (currentUp.y < -0.1f) ? -1.0f : 1.0f;

                    if (abs(currentDir.z) > abs(currentDir.x)) // Z轴主导 (Front/Back)
                    {
                        float sign = (currentDir.z >= 0.0f) ? 1.0f : -1.0f;
                        
                        // 公式说明：
                        // Top:    Front-> -Z, Back-> +Z. (sign * -1)
                        // Bottom: Front-> +Z, Back-> -Z. (sign * 1)
                        // 乘以 invert 修正倒立情况
                        if (clickedDir.y > 0) // Top
                            up = glm::vec3(0, 0, -1.0f * sign * invert);
                        else                  // Bottom
                            up = glm::vec3(0, 0, 1.0f * sign * invert);
                    }
                    else // X轴主导 (Left/Right)
                    {
                        float sign = (currentDir.x >= 0.0f) ? 1.0f : -1.0f;

                        if (clickedDir.y > 0) // Top
                            up = glm::vec3(-1.0f * sign * invert, 0, 0);
                        else                  // Bottom
                            up = glm::vec3(1.0f * sign * invert, 0, 0);
                    }
                }
                // --- 情况 B: 点击的是 Side Views (X轴 或 Z轴) ---
                else {
                    float dot = glm::dot(clickedDir, currentUp);
                    
                    // 定义动作类型
                    bool isBackFlip  = dot > 0.5f;   // 向头顶翻 (后空翻)
                    bool isFrontFlip = dot < -0.5f;  // 向脚底翻 (前空翻)
                    
                    // 定义当前位置区域
                    bool isTopHemi    = currentDir.y > 0.1f;  // 在上半球
                    bool isBottomHemi = currentDir.y < -0.1f; // 在下半球
                    
                    // 获取当前是否已经是倒立状态 (用于横向旋转保持状态)
                    bool isAlreadyUpsideDown = currentUp.y < -0.1f;

                    if (isBackFlip) {
                        // === 后空翻逻辑 ===
                        // Top    -> Back : 变倒立
                        // Bottom -> Front: 变直立
                        if (isTopHemi) up = glm::vec3(0, -1, 0); 
                        else           up = glm::vec3(0, 1, 0);  
                    }
                    else if (isFrontFlip) {
                        // === 前空翻逻辑 (这就是你遇到BUG的地方) ===
                        // Top    -> Front: 变直立
                        // Bottom -> Back : 变倒立 (修复点!)
                        if (isTopHemi) up = glm::vec3(0, 1, 0);
                        else           up = glm::vec3(0, -1, 0); 
                    }
                    else {
                        // === 横向旋转 (Yaw) ===
                        // 保持当前的直立/倒立状态
                        if (isAlreadyUpsideDown) {
                            up = glm::vec3(0, -1, 0); 
                        } else {
                            up = glm::vec3(0, 1, 0);
                        }
                    }
                }

                glm::mat4 targetViewMat = glm::lookAt(targetPos, _cameraPivot, up); 
                glm::quat targetRot = glm::quat_cast(glm::inverse(targetViewMat));

                startCameraAnimation(targetPos, targetRot, _cameraPivot);
            }
        }
        
        ImGui::End(); // End 3D Viewport
        ImGui::PopStyleVar(); // 恢复 Padding

        // =======================================================
        // 4. 左侧面板：场景大纲 (Scene Hierarchy)
        // =======================================================
        // 注意：不再设置 Pos/Size，让用户自己拖拽布局
        ImGui::Begin("Scene Hierarchy");

        if (ImGui::Button("+ Add Object"))
            ImGui::OpenPopup("AddObjPopup");
        
        if (ImGui::BeginPopup("AddObjPopup"))
        {
            if (ImGui::MenuItem("Cube")) {
                _scene->createCube(); 
            }
            if (ImGui::MenuItem("Point Light")) {
                _scene->createPointLight();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        const auto& objects = _scene->getGameObjects();
        for (int i = 0; i < objects.size(); ++i)
        {
            auto &go = objects[i]; // go 是 unique_ptr
            if (ImGui::Selectable((go->name + "##" + std::to_string(i)).c_str(), _selectedObject == go.get()))
            {
                _selectedObject = go.get();
            }
        }
        ImGui::End();

        // =======================================================
        // 5. 右侧面板：属性栏 (Inspector)
        // =======================================================
        ImGui::Begin("Inspector");

        if (_selectedObject)
        {
            drawInspector(_selectedObject);
        }
        else
        {
            // 简单的居中提示
            float availW = ImGui::GetContentRegionAvail().x;
            float textW = ImGui::CalcTextSize("No Object Selected").x;
            if (availW > textW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - textW) * 0.5f);
            
            ImGui::TextDisabled("No Object Selected");
        }

        ImGui::End();

        // [新增] 绘制资源浏览器
        renderProjectPanel();
    }

    // 6. 渲染结束 (保持不变)
    ImGui::Render();
}

void SceneRoaming::renderProjectSelector()
{
    // 获取视口中心
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();

    // 设定窗口大小
    ImVec2 windowSize(600, 300); // 稍微宽一点，高一点
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f)); // 真正的屏幕居中
    ImGui::SetNextWindowSize(windowSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    
    // [UI美化] 稍微加点圆角和阴影 (如果支持)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::Begin("Project Setup", nullptr, flags);

    // 垂直居中内容
    float contentHeight = 120.0f; // 估算内容高度
    ImGui::SetCursorPosY((windowSize.y - contentHeight) * 0.5f);

    // 大标题
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // 假设默认字体
    // 如果你有大字体，这里 Push 大字体
    float textWidth = ImGui::CalcTextSize("Select or Create Project Folder").x;
    ImGui::SetCursorPosX((windowSize.x - textWidth) * 0.5f);
    ImGui::Text("Select or Create Project Folder");
    ImGui::PopFont();
    
    ImGui::Dummy(ImVec2(0, 20)); // 间距

    // --- 路径输入行 ---
    // 动态计算宽度：总宽 - 按钮宽 - 间距 - 左右padding
    float padding = 40.0f; // 左右留白
    float btnWidth = 100.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float inputWidth = windowSize.x - (padding * 2) - btnWidth - spacing;

    ImGui::SetCursorPosX(padding); // 左对齐开始
    
    // 输入框
    ImGui::PushItemWidth(inputWidth);
    ImGui::InputText("##Path", _projectPathBuf, sizeof(_projectPathBuf));
    ImGui::PopItemWidth();

    ImGui::SameLine();

    // 浏览按钮
    if (ImGui::Button("Browse...", ImVec2(btnWidth, 0)))
    {
        IGFD::FileDialogConfig config;
        config.path = ".";
        ImGuiFileDialog::Instance()->OpenDialog("ChooseDirDlgKey", "Choose Project Directory", nullptr, config);
    }

    ImGui::Dummy(ImVec2(0, 20)); // 间距

    // --- 确认按钮 (居中) ---
    float confirmBtnWidth = 200.0f;
    ImGui::SetCursorPosX((windowSize.x - confirmBtnWidth) * 0.5f);
    
    // [UX] 如果路径为空，禁用按钮
    bool hasPath = strlen(_projectPathBuf) > 0;
    if (!hasPath) ImGui::BeginDisabled();
    
    if (ImGui::Button("Open / Create Project", ImVec2(confirmBtnWidth, 40)))
    {
        std::string path(_projectPathBuf);
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
        }
        ResourceManager::Get().setProjectRoot(path);
        _isProjectOpen = true;
    }
    
    if (!hasPath) ImGui::EndDisabled();

    ImGui::End();
    ImGui::PopStyleVar(); // Pop WindowRounding
}

void SceneRoaming::drawInspector(GameObject *obj)
{
    // 1. Name & Delete Object
   char nameBuf[128];
    strcpy(nameBuf, obj->name.c_str());

    // 1. 获取当前样式和可用宽度
    ImGuiStyle& style = ImGui::GetStyle();
    float availableWidth = ImGui::GetContentRegionAvail().x;

    // 2. 预计算按钮的宽度 (文字宽度 + 左右内边距)
    const char* btnLabel = "Delete Object"; 
    // 如果觉得 "Delete Object" 太长，可以改成 "Delete" 或 "Del"
    float buttonWidth = ImGui::CalcTextSize(btnLabel).x + style.FramePadding.x * 2.0f;

    // 3. 计算输入框应该占用的宽度
    // 输入框宽度 = 总宽 - 按钮宽 - 控件间距
    float inputWidth = availableWidth - buttonWidth - style.ItemSpacing.x;

    // 4. 设置下一个控件(InputText)的宽度
    ImGui::SetNextItemWidth(inputWidth);

    // 5. 绘制输入框
    // 注意：这里把 "Name" 改成了 "##Name"。
    // "##" 在 ImGui 中表示：作为 ID 区分，但在界面上不显示标签文字。
    // 这样就不会出现 `[框] Name [按钮]` 这种奇怪的排版了。
    if (ImGui::InputText("##Name", nameBuf, sizeof(nameBuf)))
        obj->name = nameBuf;

    // 6. 绘制按钮
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1));
    bool shouldDeleteObj = ImGui::Button(btnLabel); // 使用上面定义的 label
    ImGui::PopStyleColor();

    if (shouldDeleteObj)
    {
        _scene->removeGameObject(obj);
        _selectedObject = nullptr;
        return;
    }
    ImGui::Separator();

    // 2. Transform (内置，不可删除)
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat3("Position", glm::value_ptr(obj->transform.position), 0.1f);
        if (ImGui::DragFloat3("Rotation", glm::value_ptr(obj->transform.rotationEuler), 0.5f))
        {
            obj->transform.setRotation(obj->transform.rotationEuler);
        }
        ImGui::DragFloat3("Scale", glm::value_ptr(obj->transform.scale), 0.1f);
    }

    // 3. Components (动态列表)
    Component *compToRemove = nullptr;
    for (auto &comp : obj->components)
    {
        ImGui::PushID(comp.get()); // 防止 ID 冲突

        // 获取组件显示名称
        std::string headerName = "Unknown Component";
        if (comp->getType() == ComponentType::MeshRenderer)
            headerName = "Mesh Renderer";
        else if (comp->getType() == ComponentType::Light)
            headerName = "Light Source";

        bool isOpen = ImGui::CollapsingHeader(headerName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        if (isOpen)
        {
            // [新增] 在组件内部显示删除按钮，更直观
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Remove Component", ImVec2(-1, 0))) // -1 占满宽度
            {
                compToRemove = comp.get();
            }
            ImGui::PopStyleColor();

            ImGui::Dummy(ImVec2(0, 5)); // 增加一点间距

            // 调用具体的绘制逻辑
            drawComponentUI(comp.get());

            ImGui::Dummy(ImVec2(0, 10)); // 底部间距
        }
        ImGui::PopID();
    }

    // 执行删除组件操作
    if (compToRemove)
        obj->removeComponent(compToRemove);

    // 4. Add Component (带限制逻辑)
    ImGui::Separator();
    if (ImGui::Button("Add Component..."))
        ImGui::OpenPopup("AddCompPopup");

    if (ImGui::BeginPopup("AddCompPopup"))
    {
        // [新增] 检查是否已存在组件
        bool hasMesh = obj->getComponent<MeshComponent>() != nullptr;
        bool hasLight = obj->getComponent<LightComponent>() != nullptr;

        // 如果已有 Mesh，禁用该选项 (变灰)
        if (ImGui::MenuItem("Mesh Renderer", nullptr, false, !hasMesh))
        {
            obj->addComponent<MeshComponent>(GeometryFactory::createCube());
        }

        // 如果已有 Light，禁用该选项
        if (ImGui::MenuItem("Light Source", nullptr, false, !hasLight))
        {
            // 如果添加了光照，且已有 Mesh，通常这个 Mesh 是作为 Gizmo 存在的
            // 我们可以自动把 Mesh 标记为 Gizmo (可选)
            auto light = obj->addComponent<LightComponent>(LightType::Point);
            if (hasMesh)
            {
                auto mesh = obj->getComponent<MeshComponent>();
                mesh->isGizmo = true; // 自动设为 Gizmo 模式
            }
        }
        ImGui::EndPopup();
    }
}

void SceneRoaming::drawComponentUI(Component *comp)
{
    // --- Case 1: Mesh Renderer ---
    if (comp->getType() == ComponentType::MeshRenderer)
    {
        auto mesh = static_cast<MeshComponent *>(comp);

        ImGui::Checkbox("Is Gizmo (Unlit)", &mesh->isGizmo);
        ImGui::SameLine();
        ImGui::Checkbox("Double Sided", &mesh->doubleSided);

        // Mesh Filter 设置区域
        ImGui::Separator();
        ImGui::Text("Mesh Filter");

        // 1. 形状选择下拉菜单
        const char *shapeNames[] = {"Cube", "Sphere", "Cylinder", "Cone", "Prism", "Frustum", "Plane", "Custom OBJ"};
        int currentItem = (int)mesh->shapeType;

        bool needRebuild = false;

        if (ImGui::Combo("Shape", &currentItem, shapeNames, IM_ARRAYSIZE(shapeNames)))
        {
            mesh->shapeType = (MeshShapeType)currentItem;
            
            // 如果切到了 Plane，自动开启双面；切到别的（如 Cube），自动关闭
            if (mesh->shapeType == MeshShapeType::Plane) mesh->doubleSided = true;
            else mesh->doubleSided = false;
            
            if (mesh->shapeType != MeshShapeType::CustomOBJ)
            {
                needRebuild = true; // <--- 立即触发重建
            }
        }

        // 2. 根据类型显示不同的参数滑块
        switch (mesh->shapeType)
        {
        case MeshShapeType::Cube:
            if (ImGui::DragFloat("Size", &mesh->params.size, 0.05f, 0.01f, 10.0f))
                needRebuild = true;
            break;

        case MeshShapeType::Sphere:
            if (ImGui::DragFloat("Radius", &mesh->params.radius, 0.05f, 0.01f, 10.0f))
                needRebuild = true;
            if (ImGui::SliderInt("Slices", &mesh->params.slices, 3, 64))
                needRebuild = true;
            if (ImGui::SliderInt("Stacks", &mesh->params.stacks, 2, 64))
                needRebuild = true;
            break;

        case MeshShapeType::Cylinder:
            if (ImGui::DragFloat("Radius", &mesh->params.radius, 0.05f, 0.01f, 10.0f))
                needRebuild = true;
            if (ImGui::DragFloat("Height", &mesh->params.height, 0.05f, 0.01f, 10.0f))
                needRebuild = true;
            if (ImGui::SliderInt("Slices", &mesh->params.slices, 3, 64))
                needRebuild = true;
            break;

        case MeshShapeType::Cone:
            if (ImGui::DragFloat("Radius", &mesh->params.radius, 0.05f, 0.01f, 10.0f))
                needRebuild = true;
            if (ImGui::DragFloat("Height", &mesh->params.height, 0.05f, 0.01f, 10.0f))
                needRebuild = true;
            if (ImGui::SliderInt("Slices", &mesh->params.slices, 3, 64))
                needRebuild = true;
            break;

        case MeshShapeType::Prism: // 多面棱柱
            if (ImGui::DragFloat("Radius", &mesh->params.radius, 0.05f, 0.01f, 10.0f))
                needRebuild = true;
            if (ImGui::DragFloat("Height", &mesh->params.height, 0.05f, 0.01f, 10.0f))
                needRebuild = true;
            if (ImGui::SliderInt("Sides", &mesh->params.sides, 3, 32))
                needRebuild = true;
            break;

        case MeshShapeType::Frustum: // 多面棱台
            if (ImGui::DragFloat("Top Radius", &mesh->params.topRadius, 0.05f, 0.0f, 10.0f))
                needRebuild = true;
            if (ImGui::DragFloat("Btm Radius", &mesh->params.bottomRadius, 0.05f, 0.0f, 10.0f))
                needRebuild = true;
            if (ImGui::DragFloat("Height", &mesh->params.height, 0.05f, 0.01f, 10.0f))
                needRebuild = true;
            if (ImGui::SliderInt("Sides", &mesh->params.sides, 3, 32))
                needRebuild = true;
            break;

        case MeshShapeType::Plane:
            if (ImGui::DragFloat("Width", &mesh->params.width, 0.1f))
                needRebuild = true; // 复用 params 里的变量，或者在 struct 加 width/depth
                                    // 暂时复用 params.size 作为 width, params.height 作为 depth，或者我们在 struct 里加
                                    // 为了简单，我们复用 size=width, height=depth
            if (ImGui::DragFloat("Depth", &mesh->params.depth, 0.1f))
                needRebuild = true;
            break;

        case MeshShapeType::CustomOBJ:
            {
                // 显示当前路径 (只读，或可编辑)
                ImGui::InputText("Path", mesh->params.objPath, sizeof(mesh->params.objPath), ImGuiInputTextFlags_ReadOnly);

                // [核心] 拖拽接收区 (Drop Target)
                // 我们让整个 InputText 区域都成为接收区
                if (ImGui::BeginDragDropTarget())
                {
                    // 只接受 "ASSET_OBJ" 类型的 Payload
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_OBJ"))
                    {
                       // 1. 获取 Payload (相对路径)
                        const char* relPath = (const char*)payload->Data;
                    
                        // 2. 更新 UI 文字 (显示相对路径，比较短，好看)
                        if (strlen(relPath) < sizeof(mesh->params.objPath)) {
                            strcpy(mesh->params.objPath, relPath);
                        }

                        try {
                            auto newModel = ResourceManager::Get().getModel(relPath); // getModel 内部处理拼接

                            if (newModel) {
                                mesh->setMesh(newModel);
                            }
                        } catch(...) {}
                    }
                    ImGui::EndDragDropTarget();
                }
                
                // 提示信息
                ImGui::TextDisabled("(Drag an OBJ file here from Project panel)");
                break;
            }
        }

        // 3. 执行重建逻辑
        if (needRebuild)
        {
            std::shared_ptr<Model> newModel = nullptr;
            auto &p = mesh->params;

            switch (mesh->shapeType)
            {
            case MeshShapeType::Cube:
                newModel = GeometryFactory::createCube(p.size);
                break;
            case MeshShapeType::Sphere:
                newModel = GeometryFactory::createSphere(p.radius, p.stacks, p.slices);
                break;
            case MeshShapeType::Cylinder:
                newModel = GeometryFactory::createCylinder(p.radius, p.height, p.slices);
                break;
            case MeshShapeType::Cone:
                newModel = GeometryFactory::createCone(p.radius, p.height, p.slices);
                break;
            case MeshShapeType::Prism:
                newModel = GeometryFactory::createPrism(p.radius, p.height, p.sides);
                break;
            case MeshShapeType::Frustum:
                newModel = GeometryFactory::createPyramidFrustum(p.topRadius, p.bottomRadius, p.height, p.sides);
                break;
            case MeshShapeType::Plane:
                newModel = GeometryFactory::createPlane(p.width, p.depth);
                break;
            default:
                break;
            }

            if (newModel)
            {
                // 保持原有的 Transform 不变，只换 Mesh
                // 但是 Model 类里也有 Transform (local transform)，新建的 Model transform 是默认的
                // 如果需要保留 Model 内部的 transform (例如箭头缩放)，这里需要额外处理
                // 不过 GeometryFactory 创建出来的 Model transform 都是默认的，所以直接覆盖没问题

                // 继承旧 Model 的局部缩放? 通常不需要，GeometryFactory 出来的都是标准大小
                // 如果之前对 Gizmo 做了特殊缩放，可能会丢失，但这里是用户主动重建，重置是合理的。

                mesh->setMesh(std::move(newModel));
            }
        }

        ImGui::Separator();

        // 检查宿主是否有点光源组件
        auto lightComp = comp->owner->getComponent<LightComponent>();

        // 材质 UI
        if (ImGui::TreeNode("Material"))
        {
            if (lightComp)
            {
                // [逻辑] 如果有光源组件，强制同步颜色，并显示提示
                mesh->material.diffuse = lightComp->color;
                mesh->material.ambient = lightComp->color * 0.1f; // 简单的关联
                mesh->material.specular = glm::vec3(0.0f);        // 发光体一般没有高光

                ImGui::TextColored(ImVec4(1, 1, 0, 1), "[Locked]");
                ImGui::SameLine();
                ImGui::TextWrapped("Color is controlled by the Light Source component.");

                // 仅显示只读的颜色预览 (使用 ColorButton)
                ImGui::ColorButton("##preview", ImVec4(mesh->material.diffuse.r, mesh->material.diffuse.g, mesh->material.diffuse.b, 1.0f));
            }
            else
            {
                // [逻辑] 没有光源组件，正常显示编辑器
                ImGui::ColorEdit3("Ambient", glm::value_ptr(mesh->material.ambient));
                ImGui::ColorEdit3("Diffuse", glm::value_ptr(mesh->material.diffuse));
                ImGui::ColorEdit3("Specular", glm::value_ptr(mesh->material.specular));
            }

            // Shininess 总是可以调的
            ImGui::DragFloat("Shininess", &mesh->material.shininess, 1.0f, 1.0f, 256.0f);

            ImGui::TreePop();
        }
    }

    // --- Case 2: Light Source ---
    else if (comp->getType() == ComponentType::Light)
    {
        auto light = static_cast<LightComponent *>(comp);

        // 下拉菜单选择光源类型
        const char *typeNames[] = {"Directional", "Point", "Spot"};
        int currentType = (int)light->type;
        if (ImGui::Combo("Type", &currentType, typeNames, 3))
        {
            light->type = (LightType)currentType;
        }

        ImGui::ColorEdit3("Color", glm::value_ptr(light->color));
        ImGui::DragFloat("Intensity", &light->intensity, 0.1f, 0.0f, 10.0f);

        if (light->type == LightType::Point || light->type == LightType::Spot)
        {
            ImGui::Text("Attenuation");
            ImGui::DragFloat("Linear", &light->linear, 0.001f);
            ImGui::DragFloat("Quadratic", &light->quadratic, 0.001f);
        }

        if (light->type == LightType::Spot)
        {
            ImGui::Text("Spot Angle");
            float innerDeg = glm::degrees(glm::acos(light->cutOff));
            float outerDeg = glm::degrees(glm::acos(light->outerCutOff));

            if (ImGui::DragFloat("Inner (Deg)", &innerDeg, 0.5f, 0.0f, 180.0f))
            {
                light->cutOff = glm::cos(glm::radians(innerDeg));
            }
            if (ImGui::DragFloat("Outer (Deg)", &outerDeg, 0.5f, 0.0f, 180.0f))
            {
                light->outerCutOff = glm::cos(glm::radians(outerDeg));
            }
        }
    }
}

glm::vec3 SceneRoaming::drawViewGizmo(ImDrawList* drawList, const glm::vec3& cameraPos, const glm::mat4& viewMatrix, ImVec2 centerPos, float axisLength, bool& outGizmoHovered)
{
    // =========================================================
    // 1. 配置与配色 (尺寸加大)
    // =========================================================
    float circleRadius = 15.0f;    // [变大] 圆圈半径 (原 9.0)
    float lineThickness = 4.0f;    // [变粗] 连接线粗细 (原 2.0)
    float outlineThickness = 3.0f; // 负轴外圈粗细
    float fontSize = 23.0f;
    // 背景圆的半径 (比轴稍微大一点，包裹住整个控件)
    float bgRadius = axisLength + circleRadius * 2.0f;

    // [颜色保持不变]
    ImU32 colR = IM_COL32(240, 55, 82, 255);
    ImU32 colG = IM_COL32(110, 159, 29, 255);
    ImU32 colB = IM_COL32(47, 132, 229, 255);

    ImU32 colR_Trans = IM_COL32(240, 55, 82, 100);
    ImU32 colG_Trans = IM_COL32(110, 159, 29, 100);
    ImU32 colB_Trans = IM_COL32(47, 132, 229, 100);

    ImU32 colText = IM_COL32(0, 0, 0, 255);

    // [新增] 背景颜色 (Hover时显示淡淡的白色)
    ImU32 colBgHover = IM_COL32(255, 255, 255, 30); 
    ImU32 colBgNormal = IM_COL32(255, 255, 255, 0); // 平时透明

    // =========================================================
    // 2. 检测背景 Hover
    // =========================================================
    ImVec2 mousePos = ImGui::GetMousePos();
    float distFromCenter = sqrtf(powf(mousePos.x - centerPos.x, 2) + powf(mousePos.y - centerPos.y, 2));
    
    // 输出给外部：鼠标是否在 Gizmo 的大圆范围内
    outGizmoHovered = (distFromCenter < bgRadius);

    // 绘制背景圆 (如果在范围内，或者正在拖拽中)
    if (outGizmoHovered || _isGizmoDragging)
    {
        drawList->AddCircleFilled(centerPos, bgRadius, colBgHover);
    }

    // =========================================================
    // 3. 初始化轴数
    // =========================================================
    std::vector<GizmoAxisData> axes = {
        { {1,0,0},  colR, 0,          'X', false },
        { {0,1,0},  colG, 0,          'Y', false },
        { {0,0,1},  colB, 0,          'Z', false },
        { {-1,0,0}, colR, colR_Trans, 0,   true },
        { {0,-1,0}, colG, colG_Trans, 0,   true },
        { {0,0,-1}, colB, colB_Trans, 0,   true }
    };

    // =========================================================
    // 4. 计算变换
    // =========================================================
    glm::mat3 viewRot = glm::mat3(viewMatrix);
    for (auto& axis : axes) {
        glm::vec3 localDir = viewRot * axis.dir;
        axis.zDepth = localDir.z;
        axis.screenPos = ImVec2(
            centerPos.x + localDir.x * axisLength,
            centerPos.y - localDir.y * axisLength
        );
    }

    // =========================================================
    // 5. 排序
    // =========================================================
    std::sort(axes.begin(), axes.end(), [](const GizmoAxisData& a, const GizmoAxisData& b) {
        return a.zDepth < b.zDepth;
    });

    // 获取鼠标位置和点击状态
    bool isMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    
    // 用于记录当前鼠标悬停的轴（优先级最高的一个）
    const GizmoAxisData* hoveredAxis = nullptr;

    // =========================================================
    // 6. 绘制轴
    // =========================================================
    ImFont* font = ImGui::GetFont();
    for (const auto& axis : axes)
    {
        // --- 命中测试 (Hit Test) ---
        // 计算鼠标到圆心的距离
        float dist = sqrtf(powf(mousePos.x - axis.screenPos.x, 2) + powf(mousePos.y - axis.screenPos.y, 2));
        
        // 如果在半径内，且该轴没有被完全遮挡（zDepth > -0.8 是个经验值，防止点到背面的轴）
        // 注意：因为我们是按深度排序绘制的(后画的覆盖先画的)，
        // 所以只要鼠标在范围内，我们就更新 hoveredAxis，这样最后剩下的就是最上面的。
        if (dist <= circleRadius + 2.0f) // +2.0f 容错范围
        {
            hoveredAxis = &axis;
        }
        bool isHovered = (hoveredAxis == &axis);

        // 如果正在拖拽背景，不要高亮具体的轴，防止视觉干扰
        if (_isGizmoDragging) isHovered = false;

        if (!axis.isNegative)
        {
            // --- 正轴 ---
            if (isHovered) drawList->AddCircle(axis.screenPos, circleRadius + 2.0f, IM_COL32(255,255,255,150), 0, 2.0f);
            glm::vec2 dir2D = glm::vec2(axis.screenPos.x - centerPos.x, axis.screenPos.y - centerPos.y);
            float len = glm::length(dir2D);
            if (len > circleRadius) 
            {
                dir2D /= len;
                // 线条停在圆圈外面
                ImVec2 lineEndPos = ImVec2(
                    axis.screenPos.x - dir2D.x * (circleRadius - 1.5f), // 稍微留一点缝隙
                    axis.screenPos.y - dir2D.y * (circleRadius - 1.5f)
                );
                drawList->AddLine(centerPos, lineEndPos, axis.mainColor, lineThickness);
            }
            // 先画实心内圆 (半径减小，给描边留空间)
            drawList->AddCircleFilled(axis.screenPos, circleRadius - 1.0f, axis.mainColor);
            // 再画实心外描边 (虽然颜色一样，但加上这一层，尺寸就和负轴一样大了)
            drawList->AddCircle(axis.screenPos, circleRadius, axis.mainColor, 0, outlineThickness);

            // 文字大小不变，还是默认
            char text[2] = { axis.label, '\0' };
            ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
            // [核心修复] 视觉修正偏移量
            // y: -1.0f 通常能修正 "大写字母偏高" 的问题，把它往上提一点，抵消 Descender 的空白
            // x: 0.0f  如果觉得左右不居中，也可以微调这里
            ImVec2 opticalOffset = ImVec2(0.4f, 0.4f); 

            ImVec2 textPos = ImVec2(
                axis.screenPos.x - textSize.x * 0.5f + opticalOffset.x,
                axis.screenPos.y - textSize.y * 0.5f + opticalOffset.y
            );
            drawList->AddText(font, fontSize, textPos, colText, text);
        }
        else
        {
            // --- 负轴 ---
            if (isHovered) drawList->AddCircle(axis.screenPos, circleRadius + 2.0f, IM_COL32(255,255,255,150), 0, 2.0f);
            drawList->AddCircleFilled(axis.screenPos, circleRadius - 1.0f, axis.fillColor);
            drawList->AddCircle(axis.screenPos, circleRadius, axis.mainColor, 0, outlineThickness);
        }
    }

    // 如果正在拖拽，不允许返回点击的轴 (优先处理拖拽)
    if (_isGizmoDragging) return glm::vec3(0,0,0);

    if (isMouseClicked && hoveredAxis)
    {
        return hoveredAxis->dir;
    }

    return glm::vec3(0, 0, 0); // 没有点击
}

void SceneRoaming::renderProjectPanel()
{
    ImGui::Begin("Project / Assets");

    const auto& files = ResourceManager::Get().getFileList();
    
    // 设置缩略图/按钮大小
    float padding = 10.0f;
    float thumbnailSize = 80.0f;
    float cellSize = thumbnailSize + padding;

    // 计算一行能放几个
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::Columns(columnCount, 0, false);

    for (const auto& file : files)
    {
        std::string filename = file.first;
        std::string relativePath = file.second; // 这是相对路径，例如 "characters/bunny.obj"

        ImGui::PushID(relativePath.c_str());

        // 1. 按钮 (代表文件)
        ImGui::Button(filename.c_str(), ImVec2(80, 80));

        // [核心] 开启拖拽源 (Drag Drop Source)
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            // 设置 Payload (载荷)：传递相对路径字符串
            // "ASSET_OBJ" 是我们自定义的类型标签，接收方必须匹配这个标签
            ImGui::SetDragDropPayload("ASSET_OBJ", relativePath.c_str(), relativePath.size() + 1);

            // 拖拽时的预览图 (跟随鼠标显示的文字)
            ImGui::Text("Model: %s", filename.c_str());
            
            ImGui::EndDragDropSource();
        }

        ImGui::TextWrapped("%s", filename.c_str());
        ImGui::PopID();
        ImGui::NextColumn();
    }

    ImGui::Columns(1);
    ImGui::End();
}

// =======================================================================================
// [重写] 输入处理：实现 Unity/Blender 风格的轨道控制
// =======================================================================================
void SceneRoaming::handleInput()
{
    static bool clickLock = false;
    static bool isOperating = false;

    // =========================================================
    // [新增] 检查 ImGui 是否占用了键盘
    // =========================================================
    ImGuiIO& io = ImGui::GetIO();
    
    // 如果 ImGui 想要捕获键盘（意味着用户正在 InputText 里打字），
    // 直接返回，不处理后续的 F 键、1/3/7 键或鼠标逻辑。
    if (io.WantCaptureKeyboard) 
        return;

    // --- 惯性平滑变量 ---
    static glm::vec2 smoothOrbitDelta = glm::vec2(0.0f);
    const float friction = 30.0f; 

    // --- 快捷键逻辑 (保持不变) ---
    static bool f11Pressed = false;
    if (glfwGetKey(_window, GLFW_KEY_F11) == GLFW_PRESS) {
        if (!f11Pressed) { setFullScreen(!isFullScreen()); _isLayoutInitialized = false; f11Pressed = true; }
    } else { f11Pressed = false; }

    static bool fKeyPressed = false;
    if (glfwGetKey(_window, GLFW_KEY_F) == GLFW_PRESS) {
        if (!fKeyPressed) { frameSelectedObject(); fKeyPressed = true; }
    } else { fKeyPressed = false; }
    
    // 视图切换快捷键... (保持原样)
    static bool viewKeyPressed = false;
    bool k1 = (glfwGetKey(_window, GLFW_KEY_1) == GLFW_PRESS || glfwGetKey(_window, GLFW_KEY_KP_1) == GLFW_PRESS);
    bool k3 = (glfwGetKey(_window, GLFW_KEY_3) == GLFW_PRESS || glfwGetKey(_window, GLFW_KEY_KP_3) == GLFW_PRESS);
    bool k7 = (glfwGetKey(_window, GLFW_KEY_7) == GLFW_PRESS || glfwGetKey(_window, GLFW_KEY_KP_7) == GLFW_PRESS);
    if (k1 || k3 || k7) {
        if (!viewKeyPressed) {
            if (k1) switchToView(glm::vec3(0, 0, 1));
            if (k3) switchToView(glm::vec3(1, 0, 0));
            if (k7) switchToView(glm::vec3(0, 1, 0));
            viewKeyPressed = true;
        }
    } else { viewKeyPressed = false; }

    // =========================================================
    // 输入获取
    // =========================================================
    float dt = io.DeltaTime;

    bool allowMouse = _isViewportHovered || isOperating;
    if (!allowMouse && !_isViewportFocused) return;

    Camera* cam = _cameras[activeCameraIndex].get();

    float dx = io.MouseDelta.x;
    float dy = io.MouseDelta.y;
    float scrollX = io.MouseWheelH;
    float scrollY = io.MouseWheel;

    bool isAlt = io.KeyAlt;
    bool isCtrl = io.KeyCtrl;
    bool isShift = io.KeyShift;
    bool isLMB = io.MouseDown[0];
    bool isRMB = io.MouseDown[1];
    bool isMMB = io.MouseDown[2];

    // =========================================================
    // [Blender 风格] 设备推断逻辑 (Device Inference)
    // =========================================================
    
    // 1. 判断是否一定是触控板 (Certainly Trackpad)
    //    条件：有横向滚动，或者数值为小数
    bool isFractional = (scrollY != 0.0f) && (std::abs(scrollY - std::round(scrollY)) > 0.02f);
    bool hasHorizontal = (scrollX != 0.0f);
    
    // 2. 判断是否可能是鼠标滚轮 (Likely Mouse Wheel)
    //    条件：严格等于 1.0 或 -1.0。
    //    [关键改进]：如果数值 > 1.1 (比如用力滑出 3.0)，我们不再认为是鼠标。
    //    因为普通滚轮很难在一帧内触发多次事件(除非帧率极低)，而触控板经常这样做。
    //    防御性策略：把“高速滑动”归类为旋转，避免误触缩放。
    bool isMouseStep = (std::abs(scrollY) >= 0.9f);

    // 综合判定
    // 只有当：数值严格是1.0 且 没有横向滚动 且 没有小数特征 时，才判定为物理鼠标
    bool isPhysicalMouse = isMouseStep && !hasHorizontal && !isFractional;

    // =========================================================
    // 意图定义
    // =========================================================
    bool intentZoom = false;
    bool intentOrbit = false;
    bool intentPan = false;

    // A. 缩放意图
    //    - 按住 Ctrl (触控板捏合)
    //    - 或者是 物理鼠标滚轮 且 没按Shift
    if (isCtrl || (isPhysicalMouse && !isShift)) {
        intentZoom = true;
    }
    
    // B. 平移意图
    //    - 按住 Shift
    else if (isShift) {
        intentPan = true;
    }
    
    // C. 旋转意图
    //    - 鼠标中键按下
    //    - 或者 是任何非鼠标滚轮的输入 (触控板滑动)
    //    注意：这里 activeScroll != 0 且前面没进 Zoom 分支，就会进这里
    else if (isMMB || (scrollX != 0 || scrollY != 0)) {
        intentOrbit = true;
    }

    // =========================================================
    // 执行逻辑
    // =========================================================

    // --- 1. 平移 (Pan) ---
    if (intentPan)
    {
        isOperating = true;
        float sens = 0.002f * _currentOrbitDist;
        glm::vec3 delta(0.0f);
        glm::vec3 right = cam->transform.getRight();
        glm::vec3 up = cam->transform.getUp();

        if (isMMB) { 
            delta = (right * -dx * sens) + (up * dy * sens);
        } else {     
            float trackpadSens = 5.0f * sens; 
            delta = (right * -scrollX * trackpadSens) + (up * scrollY * trackpadSens);
        }
        cam->transform.position += delta;
        _cameraPivot += delta;
    }
    
    // --- 2. 缩放 (Zoom) ---
    else if (intentZoom)
    {
        float zoomFactor = 1.0f;
        float inputVal = scrollY != 0 ? scrollY : scrollX;

        if (isPhysicalMouse) {
            // 物理滚轮：固定步进 (10%)
            // 无论你滚多快，这里只响应一次步进，保证稳定
            zoomFactor = (inputVal > 0) ? 0.9f : 1.1f;
        } else {
            // 触控板捏合(Ctrl+Swipe)：线性平滑缩放
            // 限制最大缩放速度，防止捏合过快画面起飞
            float safeInput = glm::clamp(inputVal, -2.0f, 2.0f);
            zoomFactor = 1.0f - (safeInput * 0.3f); 
        }

        _targetOrbitDist *= zoomFactor;
        if (_targetOrbitDist < 0.1f) _targetOrbitDist = 0.1f;
    }

    // --- 3. 旋转 (Orbit) ---
    else if (intentOrbit)
    {
        isOperating = true;
        
        float targetDeltaX = 0.0f;
        float targetDeltaY = 0.0f;

        if (isMMB) {
            float mouseSens = 0.001f;
            // 鼠标中键
            targetDeltaX = -dx * mouseSens;
            targetDeltaY = -dy * mouseSens;
            // smoothOrbitDelta = glm::vec2(targetDeltaX, targetDeltaY);
        } else {
            // 触控板滑动
            // [Blender 风格] 如果输入值很大（用力滑），它被我们归类到了这里（Orbit）。
            // 这样你用力滑只会让地球转得更快，而不会导致视角突然拉远拉近。
            
            float trackpadScaleX = 0.15f;
            float trackpadScaleY = 0.12f;
            targetDeltaX = -scrollX * trackpadScaleX;
            targetDeltaY = -scrollY * trackpadScaleY;
            
            // 惯性平滑
            smoothOrbitDelta.x = glm::mix(smoothOrbitDelta.x, targetDeltaX, dt * friction);
            smoothOrbitDelta.y = glm::mix(smoothOrbitDelta.y, targetDeltaY, dt * friction);
        }

        float activeDx = isMMB ? targetDeltaX : smoothOrbitDelta.x;
        float activeDy = isMMB ? targetDeltaY : smoothOrbitDelta.y;

        rotateCamera(activeDx, activeDy);
    }
    // 衰减
    else {
        smoothOrbitDelta = glm::mix(smoothOrbitDelta, glm::vec2(0.0f), dt * 30.0f);
        if (!isLMB && !isMMB && !isRMB) isOperating = false;
    }

    // --- 点击选择 ---
    if (allowMouse && isLMB && !isShift && !isCtrl && !isAlt && !isOperating) {
        if (!clickLock) { handleMousePick(); clickLock = true; }
    } else if (!isLMB) {
        clickLock = false;
    }
}

// 注意：这里的 mouseX, mouseY 必须是相对于 Viewport 左上角的坐标
Ray SceneRoaming::screenPointToRay(float localX, float localY)
{
    // [重要] 使用 _viewportSize 而不是窗口大小
    if (_viewportSize.x <= 0 || _viewportSize.y <= 0) 
        return Ray(glm::vec3(0), glm::vec3(0,0,1));

    // NDC (-1 ~ 1)
    float x = (2.0f * localX) / _viewportSize.x - 1.0f;
    float y = 1.0f - (2.0f * localY) / _viewportSize.y; 

    // 反投影
    glm::mat4 proj = _cameras[activeCameraIndex]->getProjectionMatrix();
    glm::mat4 view = _cameras[activeCameraIndex]->getViewMatrix();
    glm::mat4 invVP = glm::inverse(proj * view);

    glm::vec4 screenPos = glm::vec4(x, y, 1.0f, 1.0f);
    glm::vec4 worldPos = invVP * screenPos;

    if (worldPos.w != 0.0f) worldPos /= worldPos.w;

    glm::vec3 dir = glm::normalize(glm::vec3(worldPos) - _cameras[activeCameraIndex]->transform.position);

    return Ray(_cameras[activeCameraIndex]->transform.position, dir);
}

void SceneRoaming::handleMousePick()
{
    // [修正 2] 增加特定的视口悬停检查
    // 只有当鼠标真的在 3D 视口图片上时，才允许点击
    if (!_isViewportHovered) 
        return;

    // 获取全局鼠标坐标
    double mouseX, mouseY;
    glfwGetCursorPos(_window, &mouseX, &mouseY);

    // [修正 3] 计算相对于视口左上角的局部坐标
    float localX = (float)mouseX - _viewportPos.x;
    float localY = (float)mouseY - _viewportPos.y;

    // 使用 Viewport 大小生成射线 (而不是 Window 大小)
    Ray worldRay = screenPointToRay(localX, localY);

    // [调试] 打印射线方向，看看是否合理 (应该是朝屏幕内的)
    std::cout << "Ray Dir: " << worldRay.direction.x << ", " 
              << worldRay.direction.y << ", " << worldRay.direction.z << std::endl;

    GameObject *closestObj = nullptr;
    float closestDist = std::numeric_limits<float>::max();

    const auto& objects = _scene->getGameObjects();
    for (const auto &go : objects)
    {
        auto meshComp = go->getComponent<MeshComponent>();
        if (!meshComp || !meshComp->enabled) continue;

        // 1. 计算 Model Matrix (包含父子变换 + Mesh 自身变换)
        glm::mat4 modelMatrix = go->transform.getLocalMatrix();
        modelMatrix = modelMatrix * meshComp->model->transform.getLocalMatrix();

        // 2. 将射线转到局部空间
        glm::mat4 invModel = glm::inverse(modelMatrix);

        // 变换原点 (Point, w=1)
        glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(worldRay.origin, 1.0f));
        
        // 变换方向 (Vector, w=0)
        // 注意：方向向量不受平移影响，但受缩放/旋转影响
        glm::vec3 localDir = glm::vec3(invModel * glm::vec4(worldRay.direction, 0.0f));
        
        // [关键] 局部空间下的射线方向必须重新归一化！
        // 因为如果物体有缩放(Scale != 1)，逆变换后的方向向量长度会变，导致 t 值计算错误。
        localDir = glm::normalize(localDir);

        Ray localRay(localOrigin, localDir);

        // 3. 检测
        float t = 0.0f;
        // 确保你的 getBoundingBox() 返回的包围盒不是空的 (min < max)
        if (PhysicsUtils::intersectRayAABB(localRay, meshComp->model->getBoundingBox(), t))
        {
            // 确保 t > 0 (物体在射线前方)
            if (t > 0.0f && t < closestDist)
            {
                closestDist = t;
                closestObj = go.get();
            }
        }
    }

    _selectedObject = closestObj;
    
    // [调试]
    if(_selectedObject) std::cout << "Picked: " << _selectedObject->name << std::endl;
}

void SceneRoaming::frameSelectedObject()
{
    if (!_selectedObject) return;

    // 1. 获取包围盒并计算真正的几何中心
    BoundingBox bounds;
    bool hasBounds = false;
    glm::vec3 centerOffset(0.0f); // 相对于物体原点的中心偏移量
    float objectRadius = 1.0f;    // 物体的包围球半径

    if (auto mesh = _selectedObject->getComponent<MeshComponent>()) {
        bounds = mesh->model->getBoundingBox();
        hasBounds = true;

        // [核心修复 1] 计算局部空间的几何中心 (比如人的中心在腰部，而不是脚底)
        glm::vec3 localCenter = (bounds.min + bounds.max) * 0.5f;
        
        // 计算考虑到缩放后的偏移
        // 注意：如果物体有旋转，中心点也需要跟着旋转
        centerOffset = _selectedObject->transform.rotation * (localCenter * _selectedObject->transform.scale);

        // 计算包围盒的最大尺寸（作为半径参考）
        glm::vec3 size = (bounds.max - bounds.min) * _selectedObject->transform.scale;
        objectRadius = glm::length(size) * 0.5f; 
    }

    // [核心修复 1 结果] 目标锚点 = 物体脚底位置 + 中心偏移
    // 这样摄像机就会盯着物体的“肚子”看，而不是盯着“脚”看，上方就不会溢出了
    glm::vec3 targetPivot = _selectedObject->transform.position + centerOffset;

    // 2. 计算合适的距离
    if (objectRadius < 0.5f) objectRadius = 0.5f;
    
    // 假设垂直 FOV 是 60 度 (30度半角)
    float halfFov = glm::radians(30.0f);
    
    // 基础距离：刚好填满垂直视口
    float dist = objectRadius / glm::sin(halfFov);

    // [核心修复 2] 增加“安全边距系数” (Padding Factor)
    // 1.0 是刚好撑满，1.2 ~ 1.5 是比较舒服的留白
    // 如果你觉得还是太近，把这个数字改大，比如 1.5f
    dist *= 1.3f; 

    // 更新平滑缩放的目标
    _targetOrbitDist = dist; 

    // =========================================================
    // 3. 计算位置和旋转 (保持刚才的“上帝视角”逻辑)
    // =========================================================
    
    // 固定的俯视角度 (前+上)
    glm::vec3 fixedDir = glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f));

    // 计算目标位置
    glm::vec3 targetPos = targetPivot + fixedDir * dist;

    // 计算该角度下的标准旋转
    glm::vec3 targetUp = glm::vec3(0, 1, 0);
    glm::mat4 targetView = glm::lookAt(targetPos, targetPivot, targetUp);
    glm::quat targetRot = glm::quat_cast(glm::inverse(targetView));

    // 触发动画 (注意：第三个参数传入的是计算好的 targetPivot，即物体中心)
    startCameraAnimation(targetPos, targetRot, targetPivot);
}

// =======================================================================================
// [新增] 平滑缩放逻辑
// =======================================================================================
void SceneRoaming::updateSmoothZoom()
{
    // 如果正在进行 Gizmo 动画，不处理缩放插值，交给 updateCameraAnimation
    if (_isCameraAnimating) return;

    // 简单的指数平滑插值 (Lerp)
    // 10.0f 是平滑速度，数值越大越快
    float smoothFactor = 10.0f * ImGui::GetIO().DeltaTime;
    
    // 如果差距很小，直接吸附，节省计算并防止抖动
    if (std::abs(_targetOrbitDist - _currentOrbitDist) < 0.01f) {
        _currentOrbitDist = _targetOrbitDist;
    } else {
        _currentOrbitDist = glm::mix(_currentOrbitDist, _targetOrbitDist, smoothFactor);
    }

    // 根据新的距离更新相机位置
    // 核心公式：Pos = Pivot + Direction * Distance
    glm::vec3 dir = glm::normalize(_cameras[activeCameraIndex]->transform.position - _cameraPivot);
    _cameras[activeCameraIndex]->transform.position = _cameraPivot + dir * _currentOrbitDist;
}

// [新增] 切换视图辅助函数
void SceneRoaming::switchToView(const glm::vec3& dir)
{
    // dir 是摄像机所在的方向 (相对于 Pivot)
    
    // 计算目标位置
    glm::vec3 targetPos = _cameraPivot + glm::normalize(dir) * _targetOrbitDist; 
    
    // [修复] 动态计算 Up 向量
    // 默认 Up 是 (0,1,0)
    glm::vec3 up = glm::vec3(0, 1, 0);

    // 如果我们要去顶视图 (0,1,0) 或 底视图 (0,-1,0)
    // 那么视线方向就是 (0,-1,0) 或 (0,1,0)，与默认 Up 平行 -> 死锁
    // 所以此时我们将 Up 改为 Z 轴 (例如 -Z，符合 Blender 习惯：顶视图时，上方是 -Z)
    if (std::abs(dir.y) > 0.9f) {
        up = glm::vec3(0, 0, -1);
    }

    // 计算目标旋转
    glm::mat4 targetViewMat = glm::lookAt(targetPos, _cameraPivot, up);
    glm::quat targetRot = glm::quat_cast(glm::inverse(targetViewMat));

    // 触发动画
    startCameraAnimation(targetPos, targetRot, _cameraPivot);
}

void SceneRoaming::recalculatePivot()
{
    // 从屏幕中心发射射线
    Ray ray = screenPointToRay(_viewportSize.x * 0.5f, _viewportSize.y * 0.5f);
    
    float closestT = 10000.0f;
    bool hit = false;

    // 遍历场景检测碰撞
    const auto& objects = _scene->getGameObjects();
    for (const auto &go : objects) {
        auto mesh = go->getComponent<MeshComponent>();
        if (mesh && mesh->enabled) {
            glm::mat4 modelMat = go->transform.getLocalMatrix() * mesh->model->transform.getLocalMatrix();
            glm::mat4 invModel = glm::inverse(modelMat);
            
            glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(ray.origin, 1.0f));
            glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(ray.direction, 0.0f)));
            
            float t = 0;
            if (PhysicsUtils::intersectRayAABB(Ray(localOrigin, localDir), mesh->model->getBoundingBox(), t)) {
                // t 是局部空间的距离，需要大致换算回世界空间距离比较大小(简单近似)
                // 或者直接用 t 如果缩放是 1
                if (t > 0 && t < closestT) {
                    closestT = t; 
                    hit = true;
                }
            }
        }
    }

    if (hit) {
        // 如果打中了物体，Pivot 设为击中点
        _cameraPivot = ray.origin + ray.direction * closestT;
    } else {
        // 如果没打中，Pivot 设为前方 10 米
        _cameraPivot = _cameras[activeCameraIndex]->transform.position + 
                       (_cameras[activeCameraIndex]->transform.getFront() * -1.0f) * 10.0f;
    }
}

// =======================================================================================
// [重写] 动画开始：准备四元数插值
// =======================================================================================
void SceneRoaming::startCameraAnimation(const glm::vec3& targetPos, const glm::quat& targetRot, const glm::vec3& targetPivot)
{
    _animStartPos = _cameras[activeCameraIndex]->transform.position;
    _animStartPivot = _cameraPivot;
    // [新增] 记录起始旋转
    _animStartRot = _cameras[activeCameraIndex]->transform.rotation;

    _animTargetPos = targetPos;
    _animTargetPivot = targetPivot;
    // [新增] 记录目标旋转
    _animTargetRot = targetRot;

    // [关键修复] 四元数最短路径检查 (Shortest Path)
    // 如果点积 < 0，说明我们要走大约 360 度的长路径，这会导致旋转“绕远路”。
    // 反转四元数可以让它走近路。
    if (glm::dot(_animStartRot, _animTargetRot) < 0.0f)
    {
        _animTargetRot = -_animTargetRot;
    }

    // 更新目标距离
    _targetOrbitDist = glm::length(targetPos - targetPivot);

    _animTime = 0.0f;
    _isCameraAnimating = true;
}

// =======================================================================================
// [重写] 动画更新：旋转驱动位置 (Rotation Driven)
// =======================================================================================
void SceneRoaming::updateCameraAnimation()
{
    if (!_isCameraAnimating) return;

    _animTime += ImGui::GetIO().DeltaTime;
    float t = _animTime / _animDuration;
    
    if (t >= 1.0f) {
        t = 1.0f;
        _isCameraAnimating = false;
        // 动画结束，强制吸附
        _cameraPivot = _animTargetPivot;
        _currentOrbitDist = _targetOrbitDist;
        _cameras[activeCameraIndex]->transform.position = _animTargetPos;
        _cameras[activeCameraIndex]->transform.rotation = _animTargetRot;
        return;
    }

    // 缓动函数 (Ease Out Quart) - 比 Cubic 更平滑一点
    float smoothT = 1.0f - pow(1.0f - t, 4.0f);

    // --------------------------------------------------------
    // [核心逻辑]
    // --------------------------------------------------------

    // 1. 插值 Pivot (注视点)
    glm::vec3 currentPivot = glm::mix(_animStartPivot, _animTargetPivot, smoothT);
    _cameraPivot = currentPivot; // 实时更新 Pivot

    // 2. 插值 距离 (Distance)
    // 我们分别计算起点的距离和终点的距离，然后插值，这样即使缩放变化也能平滑过渡
    float startDist = glm::length(_animStartPos - _animStartPivot);
    float targetDist = glm::length(_animTargetPos - _animTargetPivot);
    float currentDist = glm::mix(startDist, targetDist, smoothT);
    _currentOrbitDist = currentDist; // 同步给平滑缩放变量

    // 3. 插值 旋转 (Rotation) - 这是最关键的一步
    // Slerp 会自动处理所有复杂的轴旋转和 Up 向量变化
    glm::quat currentRot = glm::slerp(_animStartRot, _animTargetRot, smoothT);

    // 4. [反推位置] 根据 旋转 和 距离 算出摄像机位置
    // 原理：摄像机在 Pivot 后面 distance 的位置。
    // 在摄像机局部空间，"后面" 是 +Z (因为摄像机看向 -Z)
    // 所以 Offset = Rotation * (0, 0, 1) * Distance
    glm::vec3 offset = currentRot * glm::vec3(0.0f, 0.0f, 1.0f) * currentDist;
    glm::vec3 currentPos = currentPivot + offset;

    // 5. 应用结果
    _cameras[activeCameraIndex]->transform.rotation = currentRot;
    _cameras[activeCameraIndex]->transform.position = currentPos;
}

void SceneRoaming::rotateCamera(float dx, float dy)
{
    if (glm::length(glm::vec2(dx, dy)) < 0.00001f) return;

    Camera* cam = _cameras[activeCameraIndex].get();

    // 1. 定义旋转轴
    glm::vec3 worldUp = glm::vec3(0, 1, 0);
    glm::vec3 camRight = cam->transform.getRight();

    // 2. 构建增量旋转四元数
    // 旋转 1: 偏航 (Yaw) - 绕世界 Y
    glm::quat qYaw = glm::angleAxis(dx, worldUp);
    
    // 旋转 2: 俯仰 (Pitch) - 绕局部 Right
    glm::quat qPitch = glm::angleAxis(dy, camRight);

    // 合并旋转
    glm::quat qRotation = qYaw * qPitch;

    // 3. 应用旋转到 Position (绕 Pivot)
    glm::vec3 pivotToCam = cam->transform.position - _cameraPivot;
    pivotToCam = qRotation * pivotToCam; 
    cam->transform.position = _cameraPivot + pivotToCam;

    // 4. 应用旋转到 Rotation
    cam->transform.rotation = qRotation * cam->transform.rotation;
    cam->transform.rotation = glm::normalize(cam->transform.rotation);
}
