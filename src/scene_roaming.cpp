#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "scene_roaming.h"
#include "ImGuiFileDialog.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/easing.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <filesystem>
#include <algorithm> // for std::sort

#include "engine/utils/image_utils.h"

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

    // 初始化面板
    _sceneViewPanel = std::make_unique<SceneViewPanel>();
    _hierarchyPanel = std::make_unique<HierarchyPanel>();
    _inspectorPanel = std::make_unique<InspectorPanel>();
    _projectPanel = std::make_unique<ProjectPanel>();
    _envPanel = std::make_unique<EnvironmentPanel>();

    initImGui();
    // initSceneFBO 不需要在这里调，第一次 renderUI 时会根据窗口大小自动调
}

SceneRoaming::~SceneRoaming()
{
    // 在 OpenGL 上下文销毁前，清空资源缓存
    ResourceManager::Get().shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
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
    std::string fontPath = getAssetFullPath("media/fonts/Roboto-Regular.ttf");
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

    static bool isFullscreen = false;
    static int lastWindowX, lastWindowY, lastWindowW, lastWindowH;
    if (glfwGetKey(_window, GLFW_KEY_F11) == GLFW_PRESS)
    {
        // 简单的去抖动 (Debounce)，防止一帧多次触发
        static double lastTime = 0.0;
        double now = glfwGetTime();
        if (now - lastTime > 0.2) 
        {
            lastTime = now;
            isFullscreen = !isFullscreen;

            if (isFullscreen)
            {
                // 保存当前窗口位置和大小
                glfwGetWindowPos(_window, &lastWindowX, &lastWindowY);
                glfwGetWindowSize(_window, &lastWindowW, &lastWindowH);

                // 获取主显示器
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                
                // 切换到全屏
                glfwSetWindowMonitor(_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            }
            else
            {
                // 恢复窗口模式
                glfwSetWindowMonitor(_window, nullptr, lastWindowX, lastWindowY, lastWindowW, lastWindowH, 0);
            }
        }
    }

    updateContentScale();

    // =========================================================
    // 2. 开启 ImGui 新帧 (必须在所有逻辑之前)
    // =========================================================
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) 
    {
        // 直接设置延迟，无需自己写 static bool 防抖
        _screenshotDelay = 1; 
    }

    // 1. 处理输入 (委托给 SceneViewPanel)
    // 它内部会调用 _cameraController->update() 和 handleInput()
    // 需要传入 Scene 指针用于射线检测
    _sceneViewPanel->onInputUpdate(ImGui::GetIO().DeltaTime, _scene.get(), _selectedObject);

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

    // 在物理删除之前，检查选中物体是否即将死亡
    if (_scene && _selectedObject)
    {
        // 如果当前选中的物体在删除队列里，说明它将在下面这行代码中变为野指针
        if (_scene->isMarkedForDestruction(_selectedObject))
        {
            _selectedObject = nullptr; // 强制取消选中，保护指针安全
            // 可选：打印一条日志
            std::cout << "[System] Auto-deselected object pending destruction." << std::endl;
        }
    }

    if (_scene) {
        _scene->destroyMarkedObjects();
    }

    if (_screenshotDelay > 0)
    {
        // 倒计时减一
        _screenshotDelay--;
    }
    else if (_screenshotDelay == 0) // 倒计时结束，执行截屏
    {
        int w, h;
        glfwGetFramebufferSize(_window, &w, &h);
        std::string path = ResourceManager::Get().getProjectRoot() + "/screenshot.png";
        
        // 此时这一帧已经是“没有菜单”的全新一帧了
        ImageUtils::saveScreenshot(path, w, h);
        
        // 重置为 -1，停止截屏
        _screenshotDelay = -1;
    }
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
        setupDockspace();

        _sceneViewPanel->onImGuiRender(_scene.get(), _renderer.get(), _selectedObject, _contentScale);

        // 1. Hierarchy
        _hierarchyPanel->onImGuiRender(_scene, _selectedObject); // 传入引用，允许面板修改选中项

        // 2. Inspector
        // 注意：我们需要检测 Inspector 是否删除了物体
        // 如果它删除了，selectedObject 可能会悬空，这里需要一种机制处理
        // 在 InspectorPanel 里我们直接调用了 removeGameObject。
        // 为了安全，我们可以在每帧开始时检查 _selectedObject 是否还在 _scene 里（可选但推荐）
        // 或者简单的：相信用户操作流
        _inspectorPanel->onImGuiRender(_selectedObject, _scene.get());

        // 如果 Inspector 刚刚把物体删了，我们需要把 _selectedObject 置空
        // 我们可以通过检查 Scene 是否还包含它来判断，或者让 Inspector 返回状态
        // 简单的 Hack：如果 _selectedObject 变成了野指针会崩溃。
        // [修正] InspectorPanel 内部做删除时，我们无法立即把这里的指针置空。
        // 建议修改 HierarchyPanel/InspectorPanel 的逻辑，或者：
        // 在 Scene 里加一个 isValid(GameObject*) 函数进行校验。
        // 鉴于目前架构，如果 Inspector 点击删除，下一帧这个指针就失效了。
        // 最简单的修复：给 Inspector 传 _selectedObject 的**引用**，让它在删除后置空！
        
        // 3. Project
        _projectPanel->onImGuiRender();

        // 4. Environment
        _envPanel->onImGuiRender(_scene.get(), _renderer.get());
    }

    // 6. 渲染结束 (保持不变)
    ImGui::Render();
}

void SceneRoaming::setupDockspace()
{
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
            // 导入场景按钮
            if (ImGui::MenuItem("Import as Single Mesh (.obj)"))
            {
                IGFD::FileDialogConfig config;
                config.path = ResourceManager::Get().getProjectRoot();
                ImGuiFileDialog::Instance()->OpenDialog("ImportMeshKey", "Import Single Mesh", ".obj", config);
            }

            if (ImGui::MenuItem("Import as Scene (.obj)"))
            {
                IGFD::FileDialogConfig config;
                config.path = ResourceManager::Get().getProjectRoot();
                
                ImGuiFileDialog::Instance()->OpenDialog("ImportSceneKey", "Import Scene and Split Objects", ".obj", config);
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Splits the OBJ file into multiple objects based on 'o' or 'g' tags.");
            }

            // 导出场景按钮
            if (ImGui::MenuItem("Export Scene (.obj)"))
            {
                // 1. 构造保存路径 (默认保存到项目根目录)
                // 如果你想做得更高级，可以像 Open Project 那样弹出一个 ImGuiFileDialog
                std::string exportPath = ResourceManager::Get().getProjectRoot() + "/scene_export.obj";
                
                // 2. 执行导出
                if (_scene) {
                    _scene->exportToOBJ(exportPath);
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save Screenshot (.png)"))
            {
                // 1. 立即关闭当前的 Popup 菜单 (让下一帧不渲染它)
                ImGui::CloseCurrentPopup();

                // 2. 设置延迟帧数
                // 为什么是 2？
                // Frame 0 (当前帧): 菜单还在，逻辑处理完。
                // Frame 1 (下一帧): ImGui 生成了没有菜单的画面，渲染完成 -> 截屏！
                _screenshotDelay = 2; 
            }

            ImGui::Separator();

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

    // 检查并渲染文件对话框
    if (ImGuiFileDialog::Instance()->Display("ImportMeshKey"))
    {
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            // 调用单体加载逻辑 (你现有的 CustomOBJ 逻辑)
            // 这里可以直接调用 Scene 里的辅助函数，或者在这里写逻辑
            // 建议在 Scene 里加一个 importSingleMesh(path)
            if (_scene) {
                _scene->importSingleMeshFromOBJ(path);
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    // 这里的 Key "ImportSceneKey" 必须和上面 OpenDialog 里的 Key 一致
    if (ImGuiFileDialog::Instance()->Display("ImportSceneKey"))
    {
        // 如果用户点击了 OK
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            // 获取完整文件路径
            std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            
            // 调用我们刚刚写好的 Import 逻辑
            if (_scene) {
                _scene->importSceneFromOBJ(path);
            }
        }
        
        // 关闭对话框
        ImGuiFileDialog::Instance()->Close();
    }
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