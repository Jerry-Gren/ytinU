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

        bool canFlatShade = (mesh->shapeType == MeshShapeType::Cylinder || 
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
                    
                        try {
                            bool initialFlatState = false;
                            auto newModel = ResourceManager::Get().getModel(relPath, initialFlatState); // getModel 内部处理拼接

                            if (newModel) {
                                mesh->setMesh(newModel);
                                // 导入模型一般预期都是清空之前的状态
                                mesh->isGizmo = false;
                                mesh->doubleSided = false;
                                mesh->useFlatShade = initialFlatState;
                                // 2. 更新 UI 文字 (显示相对路径，比较短，好看)
                                strncpy(mesh->params.objPath, relPath, sizeof(mesh->params.objPath) - 1);
                                mesh->params.objPath[sizeof(mesh->params.objPath) - 1] = '\0';
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