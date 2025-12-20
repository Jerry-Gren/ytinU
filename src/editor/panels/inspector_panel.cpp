#include "inspector_panel.h"
#include "engine/geometry_factory.h"
#include "engine/resource_manager.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

InspectorPanel::InspectorPanel() : Panel("Inspector") {}

void InspectorPanel::onImGuiRender(GameObject*& selectedObject, Scene* sceneContext)
{
    if (!_isOpen) return;

    if (!ImGui::Begin(_title.c_str(), &_isOpen)) {
        ImGui::End();
        return;
    }

    if (selectedObject)
    {
        // 1. Name & Delete Object
        char nameBuf[128];
        strcpy(nameBuf, selectedObject->name.c_str());

        ImGuiStyle& style = ImGui::GetStyle();
        float availableWidth = ImGui::GetContentRegionAvail().x;
        const char* btnLabel = "Delete Object"; 
        float buttonWidth = ImGui::CalcTextSize(btnLabel).x + style.FramePadding.x * 2.0f;
        float inputWidth = availableWidth - buttonWidth - style.ItemSpacing.x;

        ImGui::SetNextItemWidth(inputWidth);
        if (ImGui::InputText("##Name", nameBuf, sizeof(nameBuf)))
            selectedObject->name = nameBuf;

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1));
        bool shouldDeleteObj = ImGui::Button(btnLabel);
        ImGui::PopStyleColor();

        if (shouldDeleteObj && sceneContext)
        {
            sceneContext->markForDestruction(selectedObject);
            selectedObject = nullptr;
            // 立即结束当前 Frame 的绘制，防止访问野指针
            ImGui::End(); 
            return;
        }
        else 
        {
            // 只有没删除的时候才继续绘制
            ImGui::Separator();
            drawComponents(selectedObject);
        }
    }
    else
    {
        float availW = ImGui::GetContentRegionAvail().x;
        float textW = ImGui::CalcTextSize("No Object Selected").x;
        if (availW > textW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - textW) * 0.5f);
        ImGui::TextDisabled("No Object Selected");
    }

    ImGui::End();
}

void InspectorPanel::drawComponents(GameObject* obj)
{
    // 2. Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat3("Position", glm::value_ptr(obj->transform.position), 0.1f);
        if (ImGui::DragFloat3("Rotation", glm::value_ptr(obj->transform.rotationEuler), 0.5f))
        {
            obj->transform.setRotation(obj->transform.rotationEuler);
        }
        ImGui::DragFloat3("Scale", glm::value_ptr(obj->transform.scale), 0.1f);
    }

    // 3. Components Loop
    Component *compToRemove = nullptr;
    for (auto &comp : obj->components)
    {
        ImGui::PushID(comp->getInstanceID());

        std::string headerName = "Unknown Component";
        if (comp->getType() == ComponentType::MeshRenderer) headerName = "Mesh Renderer";
        else if (comp->getType() == ComponentType::Light) headerName = "Light Source";
        else if (comp->getType() == ComponentType::ReflectionProbe) headerName = "Reflection Probe";

        bool isOpen = ImGui::CollapsingHeader(headerName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (isOpen)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Remove Component", ImVec2(-1, 0))) 
                compToRemove = comp.get();
            ImGui::PopStyleColor();

            ImGui::Dummy(ImVec2(0, 5));
            drawComponentUI(comp.get()); // 调用具体绘制
            ImGui::Dummy(ImVec2(0, 10));
        }
        ImGui::PopID();
    }

    if (compToRemove) obj->removeComponent(compToRemove);

    // 4. Add Component
    ImGui::Separator();
    if (ImGui::Button("Add Component..."))
        ImGui::OpenPopup("AddCompPopup");

    if (ImGui::BeginPopup("AddCompPopup"))
    {
        bool hasMesh = obj->getComponent<MeshComponent>() != nullptr;
        bool hasLight = obj->getComponent<LightComponent>() != nullptr;

        if (ImGui::MenuItem("Mesh Renderer", nullptr, false, !hasMesh))
            obj->addComponent<MeshComponent>(GeometryFactory::createCube());

        if (ImGui::MenuItem("Light Source", nullptr, false, !hasLight))
        {
            auto light = obj->addComponent<LightComponent>(LightType::Point);
            if (hasMesh) {
                auto mesh = obj->getComponent<MeshComponent>();
                mesh->isGizmo = true; 
            }
        }

        bool hasProbe = obj->getComponent<ReflectionProbeComponent>() != nullptr;
        if (ImGui::MenuItem("Reflection Probe", nullptr, false, !hasProbe))
        {
            obj->addComponent<ReflectionProbeComponent>();
        }
        ImGui::EndPopup();
    }
}

// [搬运] 从 SceneRoaming::drawComponentUI 原封不动搬过来
void InspectorPanel::drawComponentUI(Component *comp)
{
    // ... 这里请粘贴原 SceneRoaming.cpp 中 drawComponentUI 的完整内容 ...
    // ... 包含 Mesh Filter, Shape Combo, Light Type Combo 等几百行代码 ...
    // 注意：需要确保 geometry_factory.h 和 resource_manager.h 已包含
    // --- Case 1: Mesh Renderer ---
    if (comp->getType() == ComponentType::MeshRenderer)
    {
        auto mesh = static_cast<MeshComponent *>(comp);
        bool needRebuild = false;

        ImGui::Checkbox("Is Gizmo (Unlit)", &mesh->isGizmo);
        ImGui::SameLine();
        ImGui::Checkbox("Double Sided", &mesh->doubleSided);

        bool canFlatShade = (mesh->shapeType == MeshShapeType::Sphere ||
                             mesh->shapeType == MeshShapeType::Cylinder || 
                             mesh->shapeType == MeshShapeType::Cone ||
                             mesh->shapeType == MeshShapeType::Prism || 
                             mesh->shapeType == MeshShapeType::Frustum ||
                             mesh->shapeType == MeshShapeType::CustomOBJ);
        
        if (canFlatShade) {
            ImGui::SameLine();
            if (ImGui::Checkbox("Flat Shade", &mesh->useFlatShade)) {
                needRebuild = true;
            }
        }

        // Mesh Filter 设置区域
        ImGui::Separator();
        ImGui::Text("Mesh Filter");

        // 1. 形状选择下拉菜单
        const char *shapeNames[] = {"Cube", "Sphere", "Cylinder", "Cone", "Prism", "Frustum", "Plane", "Custom OBJ"};
        int currentItem = (int)mesh->shapeType;

        if (ImGui::Combo("Shape", &currentItem, shapeNames, IM_ARRAYSIZE(shapeNames)))
        {
            mesh->shapeType = (MeshShapeType)currentItem;
            
            switch (mesh->shapeType) {
                case MeshShapeType::Cube:
                    mesh->doubleSided = false;
                    break;
                case MeshShapeType::Sphere:
                    mesh->doubleSided = false;
                    mesh->useFlatShade = false;
                    break;
                case MeshShapeType::Cylinder:
                    mesh->doubleSided = false;
                    mesh->useFlatShade = false;
                    break;
                case MeshShapeType::Cone:
                    mesh->doubleSided = false;
                    mesh->useFlatShade = false;
                    break;
                case MeshShapeType::Prism:
                    mesh->doubleSided = false;
                    mesh->useFlatShade = true; // 硬边
                    break;
                case MeshShapeType::Frustum:
                    mesh->doubleSided = false;
                    mesh->useFlatShade = true; // 硬边
                    break;
                case MeshShapeType::Plane:
                    mesh->doubleSided = true;
                    break;
                default:
                    mesh->doubleSided = false;
                    break;
            }
            
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
                std::string fullPath = mesh->params.objPath;
                std::string fileName = std::filesystem::path(fullPath).filename().string();

                drawResourceSlot("Mesh File", fileName, fullPath, "ASSET_OBJ",
                    // OnDrop
                    [&](const std::string& path) {
                        bool initialFlatState = false;
                        // 使用 path 加载
                        auto newModel = ResourceManager::Get().getModel(path, initialFlatState);
                        if (newModel) {
                            mesh->setMesh(newModel);
                            mesh->isGizmo = false;
                            mesh->doubleSided = false;
                            mesh->useFlatShade = initialFlatState;

                            if (!newModel->hasUVs()) {
                                // 如果模型没有 UV，自动开启 Triplanar，并给一个合理的缩放
                                mesh->useTriplanar = true;
                                mesh->triplanarScale = 0.2f; // 0.2 通常适合房间大小的物体，1.0 适合小物体，可自行调整默认值
                            } else {
                                // 如果有 UV，默认使用原始 UV
                                mesh->useTriplanar = false;
                                mesh->triplanarScale = 1.0f;
                            }
                            
                            strncpy(mesh->params.objPath, path.c_str(), sizeof(mesh->params.objPath) - 1);
                            mesh->params.objPath[sizeof(mesh->params.objPath) - 1] = '\0';
                        }
                    },
                    // OnClear
                    nullptr
                );
                
                // 提示信息可以移到 Tooltip 或者保留在下方
                // ImGui::TextDisabled("(?)"); 
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
                newModel = GeometryFactory::createSphere(p.radius, p.stacks, p.slices, mesh->useFlatShade);
                break;
            case MeshShapeType::Cylinder:
                newModel = GeometryFactory::createCylinder(p.radius, p.height, p.slices, mesh->useFlatShade);
                break;
            case MeshShapeType::Cone:
                newModel = GeometryFactory::createCone(p.radius, p.height, p.slices, mesh->useFlatShade);
                break;
            case MeshShapeType::Prism:
                newModel = GeometryFactory::createPrism(p.radius, p.height, p.sides, mesh->useFlatShade);
                break;
            case MeshShapeType::Frustum:
                newModel = GeometryFactory::createPyramidFrustum(p.topRadius, p.bottomRadius, p.height, p.sides, mesh->useFlatShade);
                break;
            case MeshShapeType::Plane:
                newModel = GeometryFactory::createPlane(p.width, p.depth);
                break;
            case MeshShapeType::CustomOBJ:
                if (strlen(p.objPath) > 0) {
                    newModel = ResourceManager::Get().getModel(p.objPath, mesh->useFlatShade);
                }
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

            // 2. [新增] 纹理贴图设置 (Texture Map)
            ImGui::Spacing();
            ImGui::Separator();
        
            std::string fullPath = mesh->diffuseMap ? mesh->diffuseMap->getUri() : "";
            std::string fileName = std::filesystem::path(fullPath).filename().string();

            drawResourceSlot("Diffuse Map", fileName, fullPath, "ASSET_TEXTURE",
                // OnDrop
                [&](const std::string& path) {
                    auto tex = ResourceManager::Get().getTexture(path);
                    if (tex) mesh->diffuseMap = tex;
                },
                // OnClear
                [&]() {
                    mesh->diffuseMap = nullptr;
                }
            );

            if (mesh->diffuseMap) // 只有有纹理时才显示这些选项
            {
                ImGui::Dummy(ImVec2(0, 5));
                ImGui::Text("UV Mapping");

                if (mesh->model && !mesh->model->hasUVs()) 
                {
                    ImGui::SameLine();
                    // 黄色警告文字
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), " [!] No UVs"); 
                    // 注意：如果你没有集成 FontAwesome (ICON_FA...)，可以直接写 "[!]" 或 "No UVs"
                    
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("This model has no UV coordinates.\nStandard texture mapping will fail.\nTriplanar Mapping is highly recommended.");
                    }
                }
                
                // 开关
                ImGui::Checkbox("Use Triplanar Mapping", &mesh->useTriplanar);
                
                // 提示信息
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Auto-generate UVs based on world position.\nUseful for models with missing or bad UVs.");

                // 如果开启了，显示缩放滑块
                if (mesh->useTriplanar) {
                    ImGui::DragFloat("Tiling Scale", &mesh->triplanarScale, 0.01f, 0.01f, 10.0f);
                }
            }

            ImGui::Separator();

            // Shininess 总是可以调的
            ImGui::DragFloat("Shininess", &mesh->material.shininess, 1.0f, 1.0f, 256.0f);

            ImGui::Separator();

            ImGui::Text("Advanced (Reflection / Refraction)");

            // 反射率
            ImGui::SliderFloat("Reflectivity", &mesh->material.reflectivity, 0.0f, 1.0f);
            
            // 透明度 (控制折射混合)
            ImGui::SliderFloat("Transparency", &mesh->material.transparency, 0.0f, 1.0f);

            // 只有当开启透明时，才需要调折射率
            if (mesh->material.transparency > 0.0f)
            {
                // 提供一些常用预设
                if (ImGui::BeginCombo("IOR Preset", "Custom"))
                {
                    if (ImGui::Selectable("Air (1.00)")) mesh->material.refractionIndex = 1.00f;
                    if (ImGui::Selectable("Water (1.33)")) mesh->material.refractionIndex = 1.33f;
                    if (ImGui::Selectable("Glass (1.52)")) mesh->material.refractionIndex = 1.52f;
                    if (ImGui::Selectable("Diamond (2.42)")) mesh->material.refractionIndex = 2.42f;
                    ImGui::EndCombo();
                }
                ImGui::DragFloat("IOR", &mesh->material.refractionIndex, 0.01f, 1.0f, 3.0f);
            }

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

        if (light->type == LightType::Directional) {
            ImGui::Separator();
            ImGui::Text("Shadow Settings");
            
            // Depth Bias
            ImGui::DragFloat("Depth Bias", &light->shadowBias, 0.0001f, 0.0f, 0.1f, "%.4f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pushes the shadow away, which fixes z-fighting.");

            // Normal Bias
            ImGui::DragFloat("Normal Bias", &light->shadowNormalBias, 0.001f, 0.0f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shrinks the shadow caster along normals, which fixes acne.");

            // [新增] 剔除模式选择
            const char* cullModeNames[] = { "Cull Back", "Cull Front" };
            // 简单的逻辑映射：0 -> GL_BACK, 1 -> GL_FRONT
            int currentCull = (light->shadowCullFace == GL_FRONT) ? 1 : 0;

            if (ImGui::Combo("Shadow Culling", &currentCull, cullModeNames, 2)) {
                light->shadowCullFace = (currentCull == 1) ? GL_FRONT : GL_BACK;
            }
            
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Front: Best for solid objects (no acne).\nBack: Best for thin objects (no leaking).");
        }
        }

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

    // --- Case 3: Ref;ection Probe ---
    else if (comp->getType() == ComponentType::ReflectionProbe)
    {
        auto probe = static_cast<ReflectionProbeComponent*>(comp);
        ImGui::Text("Resolution: %d x %d", probe->resolution, probe->resolution);

        ImGui::DragFloat3("Box Size", glm::value_ptr(probe->boxSize), 0.1f, 0.1f, 100.0f);
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The size of the room/environment for correct reflections.\nAdjust this to match your walls.");
        }

        ImGui::TextDisabled("Real-time baked environment map");
    }
}

void InspectorPanel::drawResourceSlot(const char* label, 
                                      const std::string& currentName, 
                                      const std::string& fullPath,
                                      const char* payloadType,
                                      std::function<void(const std::string&)> onDrop,
                                      std::function<void()> onClear) // 允许传入 nullptr
{
    // 1. 绘制左侧标签
    ImGui::Text("%s", label);
    
    // 2. 计算布局
    bool allowClear = (onClear != nullptr); // [新增] 检查是否有清除回调
    
    float clearBtnSize = ImGui::GetFrameHeight();
    // 如果允许清除，留出 X 按钮的空间；否则占满 (-1.0f)
    float slotWidth = allowClear ? (ImGui::GetContentRegionAvail().x - clearBtnSize - 5.0f) : -1.0f;
    
    // 准备按钮文本
    std::string btnText = currentName.empty() ? "(None)" : currentName;

    // 3. 资源按钮
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f)); 
    if (ImGui::Button(btnText.c_str(), ImVec2(slotWidth, 0))) {
        // 点击逻辑 (可选)
    }
    ImGui::PopStyleVar();

    // Tooltip
    if (ImGui::IsItemHovered() && !fullPath.empty()) {
        ImGui::SetTooltip("%s", fullPath.c_str());
    }

    // 4. 拖拽接收
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
        {
            const char* path = (const char*)payload->Data;
            if (onDrop) onDrop(path);
        }
        ImGui::EndDragDropTarget();
    }

    // 5. [修改] 只有在允许清除时，才绘制 X 按钮和右键菜单
    if (allowClear) {
        ImGui::SameLine();
        if (ImGui::Button("X", ImVec2(clearBtnSize, 0))) {
            onClear();
        }
        
        // 右键清除菜单
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Clear")) {
                onClear();
            }
            ImGui::EndPopup();
        }
    }
}