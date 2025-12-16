#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
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

void SceneRoaming::updateContentScale()
{
    float x, y;
    glfwGetWindowContentScale(_window, &x, &y);
    // 取较大的那个作为主缩放比例
    _contentScale = (x > y) ? x : y;
}

SceneRoaming::SceneRoaming(const Options &options) : Application(options)
{
    _cameraController = std::make_unique<EditorCamera>(_windowWidth, _windowHeight);

    glfwSetInputMode(_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    // 在初始化 ImGui 之前，必须先获取当前显示器的缩放比例！
    // 否则 initImGui 里的字体加载逻辑会一直使用默认的 1.0f
    updateContentScale();

    // 标记项目未打开
    _isProjectOpen = false;

    // 1. 在做任何场景加载之前，先设置资源根目录！
    // 这样 ResourceManager 才知道去哪里找文件
    ResourceManager::Get().setProjectRoot(options.assetRootDir);

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
    std::cout << "[Info] Attempting to load font from: " << fontPath << std::endl;
    
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

    _cameraController->update(ImGui::GetIO().DeltaTime);

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

    // [修改] 通知 Renderer 大小变化
    _renderer->onResize(_sceneFbo.width, _sceneFbo.height);
    
    // [新增] 同时也通知 Camera 控制器大小变化 (为了更新 Projection Matrix)
    _cameraController->onResize(_sceneFbo.width, _sceneFbo.height);

    // [修改] 获取相机的调用方式变了
    Camera* renderCam = _cameraController->getActiveCamera();

    _renderer->render(*_scene,
                      renderCam,
                      _sceneFbo.fbo,
                      _sceneFbo.width,
                      _sceneFbo.height,
                      _contentScale,
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

        // 2. 计算物理像素大小 (用于 FBO 和 glViewport)
        int rawWidth = (int)(viewportPanelSize.x * _contentScale);
        int rawHeight = (int)(viewportPanelSize.y * _contentScale);

        // 如果大小发生了变化（用户拖拽了窗口），通知 FBO 重建
        // 注意：这里可能需要处理一下最小尺寸，防止缩小到 0 崩溃
        if (rawWidth != _sceneFbo.width || rawHeight != _sceneFbo.height)
        {
            resizeSceneFBO(rawWidth, rawHeight);
            
            // 通知 Renderer 和 Camera 宽高比变化
            // 注意：Camera 的 Aspect Ratio 是宽高比，无论是逻辑还是物理，比值是一样的
            _renderer->onResize(rawWidth, rawHeight);
            _cameraController->onResize(rawWidth, rawHeight); 
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
        // [修改] 所有的 Gizmo 绘制逻辑被缩减为一行！
        // =========================================================
        _cameraController->drawViewGizmo(
            glm::vec2(_viewportPos.x, _viewportPos.y), 
            glm::vec2(_viewportSize.x, _viewportSize.y)
        );
        
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

    // =========================================================
    // [新增] 检查 ImGui 是否占用了键盘
    // =========================================================
    ImGuiIO& io = ImGui::GetIO();
    
    // 如果 ImGui 想要捕获键盘（意味着用户正在 InputText 里打字），
    // 直接返回，不处理后续的 F 键、1/3/7 键或鼠标逻辑。
    if (io.WantCaptureKeyboard) 
        return;

    // --- 快捷键逻辑 (保持不变) ---
    static bool f11Pressed = false;
    if (glfwGetKey(_window, GLFW_KEY_F11) == GLFW_PRESS) {
        if (!f11Pressed) { setFullScreen(!isFullScreen()); _isLayoutInitialized = false; f11Pressed = true; }
    } else { f11Pressed = false; }

    static bool fKeyPressed = false;
    if (glfwGetKey(_window, GLFW_KEY_F) == GLFW_PRESS) {
        if (!fKeyPressed) { 
            _cameraController->frameObject(_selectedObject); 
            fKeyPressed = true; 
        }
    } else { fKeyPressed = false; }
    
    // 视图切换快捷键
    static bool viewKeyPressed = false;
    bool k1 = (glfwGetKey(_window, GLFW_KEY_1) == GLFW_PRESS || glfwGetKey(_window, GLFW_KEY_KP_1) == GLFW_PRESS);
    bool k3 = (glfwGetKey(_window, GLFW_KEY_3) == GLFW_PRESS || glfwGetKey(_window, GLFW_KEY_KP_3) == GLFW_PRESS);
    bool k7 = (glfwGetKey(_window, GLFW_KEY_7) == GLFW_PRESS || glfwGetKey(_window, GLFW_KEY_KP_7) == GLFW_PRESS);
    if (k1 || k3 || k7) {
        if (!viewKeyPressed) {
            if (k1) _cameraController->switchToView(glm::vec3(0, 0, 1));
            if (k3) _cameraController->switchToView(glm::vec3(1, 0, 0));
            if (k7) _cameraController->switchToView(glm::vec3(0, 1, 0));
            viewKeyPressed = true;
        }
    } else { viewKeyPressed = false; }

    if (_isViewportHovered || _isViewportFocused)
    {
        _cameraController->handleInput();
    }

    static bool clickLock = false;
    bool isLMB = io.MouseDown[0];
    bool isAlt = io.KeyAlt; // Alt键通常用于防止误触选择，或者你可以自己定规则

    // 如果不是在操作相机（例如没有按住 Alt），且点击了鼠标左键
    if (_isViewportHovered && isLMB && !isAlt && !ImGui::IsMouseDragging(0)) {
        if (!clickLock) { 
            handleMousePick(); 
            clickLock = true; 
        }
    } else if (!isLMB) {
        clickLock = false;
    }
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

    auto camRay = _cameraController->screenPointToRay(
        (float)mouseX, (float)mouseY, 
        _viewportPos.x, _viewportPos.y, 
        _viewportSize.x, _viewportSize.y
    );

    // 转换成 Physics Ray (如果类型不一样)
    Ray worldRay(camRay.origin, camRay.direction);

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
