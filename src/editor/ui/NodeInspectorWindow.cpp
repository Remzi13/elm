#include "editor/ui/NodeInspectorWindow.hpp"

#include "editor/NodeEditing.hpp"
#include "editor/SceneDocument.hpp"
#include "editor/ui/ComponentIcons.hpp"
#include "editor/ui/EditorWidgets.hpp"
#include "editor/ui/SceneEditShortcuts.hpp"

#include "math/Matrix.hpp"
#include "math/Rotation.hpp"

#include "graphics/Camera.hpp"

#include "model/ModelCatalog.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/Scene.hpp"
#include "Scene/ScriptComponent.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace elm {
    namespace {

        using StatusKind = SceneStatus::Kind;

        constexpr float Rad2Deg = 180.0f / 3.14159265358979323846f;
        constexpr float Deg2Rad = 3.14159265358979323846f / 180.0f;
        constexpr const char* SectionMenuPopup = "SectionMenu";
        // Icons: symbols of the icon font (EditorStyle), in UTF-8; in a label the id comes after ###
        constexpr const char* SectionMenuButton = "\xE2\x98\xB0###SectionMenu";                  // U+2630
        constexpr const char* SectionResetButton = "\xE2\x86\xBA###SectionReset";                // U+21BA
        constexpr const char* SectionRemoveButton = "\xE2\x9C\x96###SectionRemove";              // U+2716
        constexpr const char* TransformTitle = "\xE2\x9C\xA5  Transform###Transform";            // U+2725
        constexpr const char* WorldSpaceButton = "\xE2\x8A\x95###ValueSpace";                    // U+2295
        constexpr const char* LocalSpaceButton = "\xE2\x8A\x99###ValueSpace";                    // U+2299
        constexpr const char* ShownButton = "\xE2\x97\x89###Visible";                            // U+25C9
        constexpr const char* HiddenButton = "\xE2\x97\x8B###Visible";                           // U+25CB
        constexpr const char* FrameButton = "\xE2\x97\x8E###Frame";                              // U+25CE
        constexpr const char* ParentIcon = "\xE2\x86\xB0";                                       // U+21B0
        constexpr const char* BrowseButton = "\xE2\x9B\x81###Browse";                            // U+26C1
        constexpr const char* PickModuleButton = "\xE2\x96\xBE###PickModule";                    // U+25BE
        constexpr const char* EditScriptButton = "\xE2\x9C\x8E###EditScript";                    // U+270E
        constexpr const char* ReloadModuleButton = "\xE2\x86\xBB  Reload module###ReloadModule"; // U+21BB
        constexpr const char* RetryButton = "\xE2\x86\xBB  Try again###Retry";                   // U+21BB
        constexpr const char* ResetPropButton = "\xE2\x86\xBA###ResetProp";                      // U+21BA
        constexpr const char* RemovePropButton = "\xE2\x9C\x96###RemoveProp";                    // U+2716
        constexpr const char* AddComponentButton = "\xE2\x9C\x9A  Add Component###AddComponent"; // U+271A
        constexpr const char* AddColliderButton = "\xE2\x9C\x9A  Collider###AddCollider";        // U+271A
        constexpr const char* AddRigidBodyButton = "\xE2\x9C\x9A  RigidBody###AddRigidBody";     // U+271A
        constexpr const char* ModelFileExtensions = ".model;.gltf;.glb";
        constexpr math::Vector4 DefaultModelColor { 0.8f, 0.8f, 0.8f, 1.0f };

        const ImVec4 ScriptErrorColor { 1.0f, 0.40f, 0.36f, 1.0f };
        const ImVec4 ScriptRunningColor { 0.40f, 0.85f, 0.50f, 1.0f };
        const ImVec4 NoticeColor { 1.0f, 0.75f, 0.3f, 1.0f };
        /// Props whose value is set on this node, not taken from the script.
        const ImVec4 OverrideColor { 0.55f, 0.75f, 1.0f, 1.0f };

        enum class ComponentAction : uint8_t {
            None,
            Reset,
            Remove
        };

        bool ContainsNoCase(StringView text, StringView part)
        {
            if (part.empty())
                return true;
            auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
            return std::search(text.begin(), text.end(), part.begin(), part.end(),
                       [&](char a, char b) { return lower(a) == lower(b); })
                != text.end();
        }

        /// What is shown of the label, without the id after ##.
        float SmallButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }

        /// What the header of a component's section says.
        struct SectionHeader {
            bool open { false };
            ComponentAction action { ComponentAction::None };
        };

        /// The icon and the name of a component, for its header and the Add Component menu; the name is the id.
        String ComponentLabel(ComponentType type)
        {
            const String name = ComponentTypeName(type);
            return String(ui::ComponentIcon(type)) + "  " + name + "###" + name;
        }

        /// The header of a component's section and the buttons at its right edge: the section's own menu when it has
        /// one (the caller draws SectionMenuPopup), reset and remove.
        [[nodiscard]] SectionHeader DrawSectionHeader(ComponentType type, bool hasMenu)
        {
            SectionHeader header;
            header.open = ImGui::CollapsingHeader(ComponentLabel(type).c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float removeX = ImGui::GetContentRegionMax().x - SmallButtonWidth(SectionRemoveButton);
            const float resetX = removeX - spacing - SmallButtonWidth(SectionResetButton);
            if (hasMenu) {
                ImGui::SameLine(resetX - spacing - SmallButtonWidth(SectionMenuButton));
                if (ImGui::SmallButton(SectionMenuButton))
                    ImGui::OpenPopup(SectionMenuPopup);
                ImGui::SetItemTooltip("More actions");
            }
            ImGui::SameLine(resetX);
            if (ImGui::SmallButton(SectionResetButton))
                header.action = ComponentAction::Reset;
            ImGui::SetItemTooltip("Reset the component to its defaults");
            ImGui::SameLine(removeX);
            if (ImGui::SmallButton(SectionRemoveButton))
                header.action = ComponentAction::Remove;
            ImGui::SetItemTooltip("Remove the component");
            return header;
        }

        RigidBody NewRigidBody()
        {
            return RigidBody {
                .type = BodyType::Dynamic,
                .mass = 1.0f,
                .friction = 0.5f,
                .restitution = 0.0f,
                .linearDamping = 0.05f,
                .angularDamping = 0.05f,
                .gravityScale = 1.0f,
            };
        }

        const Model* FindNodeModel(const Scene& scene, const ModelCatalog& models, NodeId node)
        {
            const auto* store = scene.FindComponents<ModelComponent>();
            const ModelComponent* component = store ? store->Find(node) : nullptr;
            return component ? models.Get(component->model) : nullptr;
        }

        String NodeLabel(const Scene::Node& node, const Scene& scene)
        {
            if (!node.name.empty())
                return node.name;
            return String("Node ") + std::to_string(scene.IndexOf(node.id)).c_str();
        }

        String FormatModelLabel(const Model* model, ModelHandler handle)
        {
            if (!model)
                return String("No Model");
            String name = model->name.empty() ? (String("Model #") + std::to_string(handle.Raw()).c_str()) : model->name;
            name += " (";
            name += std::to_string(model->submeshes.size()).c_str();
            name += " submeshes)";
            return name;
        }

        bool DrawNodeValue(NodeId& item, Scene& scene)
        {
            const Scene::Node* target = scene.Find(item);
            const String preview = target ? NodeLabel(*target, scene) : (item ? String("(Deleted)") : String("None"));
            bool changed = false;
            if (ImGui::BeginCombo("##Value", preview.c_str())) {
                if (ImGui::Selectable("None", !item)) {
                    item = NodeId {};
                    changed = true;
                }
                for (const auto& node : scene.Nodes()) {
                    ImGui::PushID(static_cast<int>(node.id.Raw()));
                    if (ImGui::Selectable(NodeLabel(node, scene).c_str(), item == node.id)) {
                        item = node.id;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool DrawScriptValue(ScriptValue& value, Scene& scene)
        {
            return std::visit([&]<typename V>(V& item) -> bool {
                if constexpr (std::is_same_v<V, bool>) {
                    return ImGui::Checkbox("##Value", &item);
                } else if constexpr (std::is_same_v<V, int64_t>) {
                    return ImGui::DragScalar("##Value", ImGuiDataType_S64, &item, 0.2f);
                } else if constexpr (std::is_same_v<V, double>) {
                    return ImGui::DragScalar("##Value", ImGuiDataType_Double, &item, 0.02f, nullptr, nullptr, "%.3f");
                } else if constexpr (std::is_same_v<V, String>) {
                    return ui::InputString("##Value", item);
                } else if constexpr (std::is_same_v<V, math::Vector3>) {
                    return ui::DragAxes("##Value", &item.x, 0.05f);
                } else if constexpr (std::is_same_v<V, math::Vector4>) {
                    return ImGui::ColorEdit4("##Value", &item.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                } else if constexpr (std::is_same_v<V, NodeId>) {
                    return DrawNodeValue(item, scene);
                }
            },
                value);
        }

    } // namespace

    NodeInspectorWindow::NodeInspectorWindow(Settings& settings, const NodeInspectorDeps& deps, Folders folders)
        : IImGuiWindow(settings)
        , m_deps(deps)
        , m_folders(std::move(folders))
    {
    }

    bool NodeInspectorWindow::IsPlaying() const
    {
        return m_deps.play.isPlaying && m_deps.play.isPlaying();
    }

    void NodeInspectorWindow::Render(render::FrameWriter&)
    {
        if (!m_visible)
            return;

        ImGui::SetNextWindowPos(ImVec2(340.0f, 60.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Name, &m_visible)) {
            ImGui::End();
            return;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            HandleSceneEditShortcuts(m_deps.edits, m_deps.sceneFile);

        const NodeId node = m_deps.selection.node;
        if (m_deps.scene.Find(node)) {
            DrawNodeRows(node);
            DrawTransformSection(node);
            DrawModelSection(node);
            DrawRigidBodySection(node);
            DrawColliderSection(node);
            DrawScriptSection(node);
            DrawAddComponent(node);
        } else {
            ImGui::TextDisabled("No node selected");
            ImGui::TextWrapped("Select a node in Scene Hierarchy to edit its transform and components.");
        }
        DrawModelDialog();
        ImGui::End();
    }

    void NodeInspectorWindow::DrawNodeRows(NodeId node)
    {
        Scene& scene = m_deps.scene;
        const ImGuiStyle& style = ImGui::GetStyle();
        ModelComponent* model = scene.Components<ModelComponent>().Find(node);
        // The name takes what the buttons after it leave
        float buttonsWidth = ImGui::GetFrameHeight() + style.ItemSpacing.x;
        if (model)
            buttonsWidth += ImGui::GetFrameHeight() + style.ItemSpacing.x;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonsWidth);
        String name = scene.Find(node)->name;
        if (ui::InputString("##Name", name))
            (void)scene.SetName(node, name);
        ImGui::SetItemTooltip("Name");
        const ImVec2 square(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
        if (model) {
            ImGui::SameLine();
            if (ImGui::Button(model->visible ? ShownButton : HiddenButton, square))
                model->visible = !model->visible;
            ImGui::SetItemTooltip(model->visible ? "Shown: click to hide the model" : "Hidden: click to show the model");
        }
        ImGui::SameLine();
        // In Play the camera is the game's
        ImGui::BeginDisabled(IsPlaying());
        if (ImGui::Button(FrameButton, square))
            FrameNode(m_deps.camera, scene, m_deps.models, node);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Bring the node into the view (F in the viewport and the tree)");

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", ParentIcon);
        ImGui::SetItemTooltip("Parent");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        DrawParentCombo(node);
    }

    void NodeInspectorWindow::DrawParentCombo(NodeId node)
    {
        Scene& scene = m_deps.scene;
        const NodeId parent = scene.Find(node)->parent;
        const Scene::Node* current = scene.Find(parent);
        const String preview = current ? NodeLabel(*current, scene) : String("None (Root)");
        if (!ImGui::BeginCombo("##Parent", preview.c_str()))
            return;
        // Applied after the list: moving a node reorders the nodes it goes through
        std::optional<NodeId> chosen;
        if (ImGui::Selectable("None (Root)", !parent))
            chosen = NodeId {};
        for (const auto& candidate : scene.Nodes()) {
            if (candidate.id == node)
                continue;
            ImGui::PushID(static_cast<int>(candidate.id.Raw()));
            if (ImGui::Selectable(NodeLabel(candidate, scene).c_str(), parent == candidate.id))
                chosen = candidate.id;
            ImGui::PopID();
        }
        ImGui::EndCombo();
        if (!chosen)
            return;
        const auto result = scene.SetParent(node, *chosen);
        if (result == Scene::SetParentResult::Cycle)
            m_deps.document.Report(StatusKind::Failure, "Cannot reparent: cycle detected.");
        else if (result == Scene::SetParentResult::SingularParent)
            m_deps.document.Report(StatusKind::Failure, "Cannot reparent: singular parent transform.");
    }

    void NodeInspectorWindow::DrawTransformSection(NodeId node)
    {
        ImGui::PushID("Transform");
        const bool open = ImGui::CollapsingHeader(TransformTitle, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        // Which space the values are in, then the menu of resets
        const float menuX = ImGui::GetContentRegionMax().x - SmallButtonWidth(SectionMenuButton);
        ImGui::SameLine(menuX - ImGui::GetStyle().ItemSpacing.x - SmallButtonWidth(WorldSpaceButton));
        if (ImGui::SmallButton(m_editLocalTransform ? LocalSpaceButton : WorldSpaceButton))
            m_editLocalTransform = !m_editLocalTransform;
        ImGui::SetItemTooltip(m_editLocalTransform ? "Local: the values are relative to the parent. Click for the world's" : "World: the values are in the world. Click for the parent's");
        ImGui::SameLine(menuX);
        if (ImGui::SmallButton(SectionMenuButton))
            ImGui::OpenPopup(SectionMenuPopup);
        ImGui::SetItemTooltip("Reset the transform");

        if (ImGui::BeginPopup(SectionMenuPopup)) {
            const Scene& scene = m_deps.scene;
            const math::Matrix4x4 current = m_editLocalTransform ? scene.Find(node)->localTransform : scene.WorldTransforms()[scene.IndexOf(node)];
            const math::DecomposedTRS trs = math::DecomposeTRS(current);
            if (ImGui::MenuItem("Reset Position"))
                WriteTransform(node, math::ComposeTRS({ 0.0f, 0.0f, 0.0f }, trs.rotation, trs.scale));
            if (ImGui::MenuItem("Reset Rotation"))
                WriteTransform(node, math::ComposeTRS(trs.translation, math::QuaternionFromEuler({ 0.0f, 0.0f, 0.0f }), trs.scale));
            if (ImGui::MenuItem("Reset Scale"))
                WriteTransform(node, math::ComposeTRS(trs.translation, trs.rotation, { 1.0f, 1.0f, 1.0f }));
            if (ImGui::MenuItem("Reset All"))
                WriteTransform(node, math::Matrix4x4::Identity());
            ImGui::EndPopup();
        }
        if (open)
            DrawTransformRows(node);
        ImGui::PopID();
    }

    void NodeInspectorWindow::DrawTransformRows(NodeId node)
    {
        if (!ui::BeginPropertyGrid())
            return;
        const Scene& scene = m_deps.scene;
        const math::Matrix4x4 current = m_editLocalTransform ? scene.Find(node)->localTransform : scene.WorldTransforms()[scene.IndexOf(node)];
        const math::DecomposedTRS trs = math::DecomposeTRS(current);
        const math::Vector3 euler = math::ToEuler(trs.rotation);

        float position[3] = { trs.translation.x, trs.translation.y, trs.translation.z };
        float rotation[3] = { euler.x * Rad2Deg, euler.y * Rad2Deg, euler.z * Rad2Deg };
        float scale[3] = { trs.scale.x, trs.scale.y, trs.scale.z };

        bool changed = false;
        ui::PropertyRow("Position");
        changed |= ui::DragAxes("##Position", position, 0.05f);
        ui::PropertyRow("Rotation");
        changed |= ui::DragAxes("##Rotation", rotation, 0.5f, 0.0f, 0.0f, "%.1f deg");
        ui::PropertyRow("Scale");
        changed |= ui::DragAxes("##Scale", scale, 0.02f, 0.001f, 1000.0f);
        ui::EndPropertyGrid();

        if (changed) {
            const math::Vector3 radians { rotation[0] * Deg2Rad, rotation[1] * Deg2Rad, rotation[2] * Deg2Rad };
            WriteTransform(node, math::ComposeTRS({ position[0], position[1], position[2] }, math::QuaternionFromEuler(radians), { scale[0], scale[1], scale[2] }));
        }
    }

    void NodeInspectorWindow::WriteTransform(NodeId node, const math::Matrix4x4& transform)
    {
        Scene& scene = m_deps.scene;
        if (m_editLocalTransform) {
            (void)scene.SetLocalTransform(node, transform);
            scene.UpdateWorldTransforms();
        } else if (!scene.SetWorldTransform(node, transform)) {
            m_deps.document.Report(StatusKind::Failure, "Cannot update transform: parent transform is singular.");
        }
    }

    void NodeInspectorWindow::DrawModelSection(NodeId node)
    {
        Scene& scene = m_deps.scene;
        ModelComponent* component = scene.Components<ModelComponent>().Find(node);
        if (!component)
            return;
        ImGui::PushID("Model");
        const SectionHeader header = DrawSectionHeader(ComponentType::Model, true);
        const bool open = header.open;
        const ComponentAction action = header.action;
        if (ImGui::BeginPopup(SectionMenuPopup)) {
            if (ImGui::BeginMenu("Set Primitive")) {
                for (const ModelPrimitive primitive : ModelPrimitives) {
                    if (ImGui::MenuItem(ModelPrimitiveName(primitive)))
                        component->model = m_deps.models.Insert(MakePrimitiveModel(primitive));
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Open in Model Editor", nullptr, false, m_deps.models.Get(component->model) != nullptr) && m_deps.files.sceneModel)
                m_deps.files.sceneModel(component->model);
            ImGui::EndPopup();
        }
        if (action == ComponentAction::Remove) {
            scene.Components<ModelComponent>().Remove(node);
        } else {
            if (action == ComponentAction::Reset) {
                component->color = DefaultModelColor;
                component->visible = true;
            }
            if (open)
                DrawModelRows(node);
        }
        ImGui::PopID();
    }

    void NodeInspectorWindow::DrawModelRows(NodeId node)
    {
        if (!ui::BeginPropertyGrid())
            return;
        ModelCatalog& models = m_deps.models;
        ModelComponent& component = *m_deps.scene.Components<ModelComponent>().Find(node);
        const Model* model = models.Get(component.model);

        ui::PropertyRow("Model");
        const ImGuiStyle& style = ImGui::GetStyle();
        const float browseWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - style.ItemSpacing.x);
        if (ImGui::BeginCombo("##Model", FormatModelLabel(model, component.model).c_str())) {
            models.ForEach([&](ModelHandler handle, const Model& item) {
                ImGui::PushID(static_cast<int>(handle.Raw()));
                if (ImGui::Selectable(FormatModelLabel(&item, handle).c_str(), component.model == handle))
                    component.model = handle;
                ImGui::PopID();
            });
            ImGui::EndCombo();
        }
        if (model) {
            const auto bounds = model->GetLocalBounds();
            if (bounds.minBounds.x <= bounds.maxBounds.x) {
                const math::Vector3 size = bounds.maxBounds - bounds.minBounds;
                ImGui::SetItemTooltip("Dimensions: %.2f x %.2f x %.2f", size.x, size.y, size.z);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(BrowseButton, ImVec2(browseWidth, 0.0f)))
            OpenModelDialog(node);
        ImGui::SetItemTooltip("A model from disk");

        ui::PropertyRow("Color");
        ImGui::ColorEdit4("##Color", &component.color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ui::EndPropertyGrid();
    }

    void NodeInspectorWindow::OpenModelDialog(NodeId node)
    {
        m_modelDialogNode = node;
        m_modelDialog.Open("Select Model File", ModelFileExtensions, m_folders.models);
    }

    void NodeInspectorWindow::DrawModelDialog()
    {
        const auto file = m_modelDialog.Draw();
        if (!file)
            return;
        // A .model of the project stays its file; anything else is imported into the project
        const auto model = ModelFromFile(m_deps.models, *file, m_folders.assets);
        if (!model) {
            m_deps.document.Report(StatusKind::Failure, "Failed to load model: " + model.error().message);
            return;
        }
        const ModelHandler handle = *model;
        const String modelName = m_deps.models.Get(handle)->name;
        Scene& scene = m_deps.scene;
        const Scene::Node* node = scene.Find(m_modelDialogNode);
        if (!node)
            return;
        auto& components = scene.Components<ModelComponent>();
        // Add Component asks for the file first: the node has no component until one is chosen
        ModelComponent* component = components.Find(m_modelDialogNode);
        if (!component)
            component = &components.Emplace(m_modelDialogNode, ModelComponent { .model = handle, .color = DefaultModelColor, .visible = true });
        component->model = handle;
        if (node->name.empty() || node->name == "Cube" || node->name == "Empty Node" || node->name == "Node" || node->name == "Model")
            (void)scene.SetName(m_modelDialogNode, modelName);
        m_deps.document.Report(StatusKind::Info, "Model assigned from " + String(file->Filename().c_str()) + ".");
    }

    void NodeInspectorWindow::DrawRigidBodySection(NodeId node)
    {
        Scene& scene = m_deps.scene;
        RigidBody* body = scene.Components<RigidBody>().Find(node);
        if (!body)
            return;
        ImGui::PushID("RigidBody");
        const SectionHeader header = DrawSectionHeader(ComponentType::RigidBody, false);
        const bool open = header.open;
        const ComponentAction action = header.action;
        if (action == ComponentAction::Remove) {
            scene.Components<RigidBody>().Remove(node);
        } else {
            if (action == ComponentAction::Reset)
                *body = NewRigidBody();
            if (open)
                DrawRigidBodyRows(node);
        }
        ImGui::PopID();
    }

    void NodeInspectorWindow::DrawRigidBodyRows(NodeId node)
    {
        Scene& scene = m_deps.scene;
        if (ui::BeginPropertyGrid()) {
            RigidBody& body = *scene.Components<RigidBody>().Find(node);
            const char* types[] = { "Static", "Kinematic", "Dynamic" };
            int type = static_cast<int>(body.type);
            ui::PropertyRow("Type");
            if (ImGui::Combo("##Type", &type, types, IM_ARRAYSIZE(types)))
                body.type = static_cast<BodyType>(type);
            ImGui::SetItemTooltip("Static: immovable geometry\nKinematic: moves by script/animation\nDynamic: simulated physics body");

            ImGui::BeginDisabled(body.type != BodyType::Dynamic);
            ui::PropertyRow("Mass");
            ImGui::DragFloat("##Mass", &body.mass, 0.05f, 0.001f, 1.0e6f, "%.3f kg", ImGuiSliderFlags_AlwaysClamp);
            ui::PropertyRow("Gravity Scale");
            ImGui::DragFloat("##GravityScale", &body.gravityScale, 0.02f, -100.0f, 100.0f, "%.2f");
            ui::PropertyRow("Linear Damping");
            ImGui::SliderFloat("##LinearDamping", &body.linearDamping, 0.0f, 1.0f, "%.3f");
            ui::PropertyRow("Angular Damping");
            ImGui::SliderFloat("##AngularDamping", &body.angularDamping, 0.0f, 1.0f, "%.3f");
            ImGui::EndDisabled();

            ui::PropertyRow("Friction");
            ImGui::DragFloat("##Friction", &body.friction, 0.01f, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ui::PropertyRow("Restitution");
            ImGui::SliderFloat("##Restitution", &body.restitution, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("Bounciness");
            ui::EndPropertyGrid();
        }
        const auto* colliders = scene.FindComponents<Collider>();
        if (!colliders || !colliders->Contains(node)) {
            ImGui::TextColored(NoticeColor, "A RigidBody needs a Collider to collide.");
            if (ImGui::SmallButton(AddColliderButton))
                scene.Components<Collider>().Emplace(node, FitColliderToModel(FindNodeModel(scene, m_deps.models, node)));
        }
    }

    void NodeInspectorWindow::DrawColliderSection(NodeId node)
    {
        Scene& scene = m_deps.scene;
        Collider* collider = scene.Components<Collider>().Find(node);
        if (!collider)
            return;
        const Model* model = FindNodeModel(scene, m_deps.models, node);
        ImGui::PushID("Collider");
        const SectionHeader header = DrawSectionHeader(ComponentType::Collider, true);
        const bool open = header.open;
        const ComponentAction action = header.action;
        if (ImGui::BeginPopup(SectionMenuPopup)) {
            if (ImGui::MenuItem("Fit to Model", nullptr, false, model != nullptr)) {
                Collider fitted = FitColliderToModel(model);
                fitted.shape = collider->shape;
                fitted.trigger = collider->trigger;
                *collider = fitted;
            }
            ImGui::EndPopup();
        }
        if (action == ComponentAction::Remove) {
            scene.Components<Collider>().Remove(node);
        } else {
            if (action == ComponentAction::Reset)
                *collider = FitColliderToModel(model);
            if (open)
                DrawColliderRows(node);
        }
        ImGui::PopID();
    }

    void NodeInspectorWindow::DrawColliderRows(NodeId node)
    {
        Scene& scene = m_deps.scene;
        if (ui::BeginPropertyGrid()) {
            Collider& collider = *scene.Components<Collider>().Find(node);
            const char* shapes[] = { "Box", "Sphere", "Capsule" };
            int shape = static_cast<int>(collider.shape);
            ui::PropertyRow("Shape");
            if (ImGui::Combo("##Shape", &shape, shapes, IM_ARRAYSIZE(shapes)))
                collider.shape = static_cast<ColliderShape>(shape);

            constexpr float MinSize = 0.001f;
            constexpr float MaxSize = 1.0e6f;
            switch (collider.shape) {
            case ColliderShape::Box:
                ui::PropertyRow("Half Extents");
                (void)ui::DragAxes("##HalfExtents", &collider.halfExtents.x, 0.02f, MinSize, MaxSize);
                break;
            case ColliderShape::Sphere:
                ui::PropertyRow("Radius");
                ImGui::DragFloat("##Radius", &collider.radius, 0.02f, MinSize, MaxSize, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                break;
            case ColliderShape::Capsule:
                ui::PropertyRow("Radius");
                ImGui::DragFloat("##Radius", &collider.radius, 0.02f, MinSize, MaxSize, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                ui::PropertyRow("Half Height");
                ImGui::DragFloat("##HalfHeight", &collider.halfHeight, 0.02f, MinSize, MaxSize, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                break;
            }
            ui::PropertyRow("Offset");
            (void)ui::DragAxes("##Offset", &collider.offset.x, 0.02f);
            ui::PropertyRow("Trigger");
            ImGui::Checkbox("##Trigger", &collider.trigger);
            ImGui::SetItemTooltip("Reports collision events on enter and exit, pushes nothing.");
            ui::EndPropertyGrid();
        }
        const auto* bodies = scene.FindComponents<RigidBody>();
        if (!bodies || !bodies->Contains(node)) {
            ImGui::TextDisabled("Static: immovable collision geometry");
            if (ImGui::SmallButton(AddRigidBodyButton))
                scene.Components<RigidBody>().Emplace(node, NewRigidBody());
        }
    }

    void NodeInspectorWindow::DrawScriptSection(NodeId node)
    {
        Scene& scene = m_deps.scene;
        if (!scene.Components<ScriptComponent>().Find(node))
            return;
        ImGui::PushID("Script");
        const SectionHeader header = DrawSectionHeader(ComponentType::Script, false);
        const bool open = header.open;
        const ComponentAction action = header.action;
        if (action == ComponentAction::Remove) {
            // The instance ends first, as with entity:remove("Script")
            if (m_deps.scripts.endInstance)
                m_deps.scripts.endInstance(node);
            scene.Components<ScriptComponent>().Remove(node);
        } else {
            if (action == ComponentAction::Reset)
                ResetScriptProps(node);
            if (open)
                DrawScriptRows(node);
        }
        ImGui::PopID();
    }

    void NodeInspectorWindow::ResetScriptProps(NodeId node)
    {
        ScriptComponent& script = *m_deps.scene.Components<ScriptComponent>().Find(node);
        script.props.clear();
        if (!m_deps.scripts.defaults || !m_deps.scripts.setField)
            return;
        if (const auto defaults = m_deps.scripts.defaults(script.module)) {
            for (const auto& [name, value] : *defaults)
                m_deps.scripts.setField(node, name, value);
        }
    }

    void NodeInspectorWindow::DrawScriptRows(NodeId node)
    {
        if (ui::BeginPropertyGrid()) {
            ui::PropertyRow("Module");
            DrawScriptModulePicker(*m_deps.scene.Components<ScriptComponent>().Find(node));
            ui::EndPropertyGrid();
        }
        DrawScriptInstance(node);
        DrawScriptProps(node);
    }

    void NodeInspectorWindow::DrawScriptModulePicker(ScriptComponent& script)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 square(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (square.x + style.ItemSpacing.x) * 2.0f);
        String module = script.module;
        if (ui::InputString("##Module", module))
            script.module = module;
        ImGui::SameLine();
        if (ImGui::Button(PickModuleButton, square)) {
            m_scriptModules = ListScriptModules(m_folders.scripts);
            m_scriptFilter[0] = '\0';
            ImGui::OpenPopup("PickModulePopup");
        }
        ImGui::SetItemTooltip("Pick a script of the project");
        ImGui::SameLine();
        ImGui::BeginDisabled(script.module.empty() || !m_deps.files.script);
        if (ImGui::Button(EditScriptButton, square)) {
            // Looked up on the click, not asked of the disk every frame
            const Path file = ScriptModuleFile(m_folders.scripts, script.module);
            if (file.Empty())
                m_deps.document.Report(StatusKind::Failure, "No file for the script module " + script.module + ".");
            else
                m_deps.files.script(file);
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Open the script in the Scripts window");
        if (ImGui::BeginPopup("PickModulePopup")) {
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##ScriptFilter", "Filter scripts...", m_scriptFilter, sizeof(m_scriptFilter));
            ImGui::Separator();
            if (m_scriptModules.empty())
                ImGui::TextDisabled("No .lua scripts found in assets/scripts");
            for (const auto& item : m_scriptModules) {
                if (ContainsNoCase(item, m_scriptFilter) && ImGui::MenuItem(item.c_str()))
                    script.module = item;
            }
            ImGui::EndPopup();
        }
    }

    void NodeInspectorWindow::DrawScriptInstance(NodeId node)
    {
        const ScriptInspection& scripts = m_deps.scripts;
        if (!IsPlaying()) {
            ImGui::TextDisabled("Starts on Play");
            return;
        }
        const ScriptInstanceState state = scripts.instance ? scripts.instance(node) : ScriptInstanceState {};
        if (state.kind == ScriptInstanceState::Kind::Faulted) {
            ImGui::TextColored(ScriptErrorColor, "Stopped by an error until a module reloads");
            ImGui::PushStyleColor(ImGuiCol_Text, ScriptErrorColor);
            ImGui::TextWrapped("%s", state.error.c_str());
            ImGui::PopStyleColor();
            if (ImGui::SmallButton(ReloadModuleButton) && scripts.reloadModule)
                scripts.reloadModule(state.module);
        } else if (state.kind == ScriptInstanceState::Kind::Running) {
            ImGui::TextColored(ScriptRunningColor, "Running");
        } else {
            ImGui::TextDisabled("Starting");
        }
    }

    void NodeInspectorWindow::DrawScriptProps(NodeId node)
    {
        const ScriptInspection& scripts = m_deps.scripts;
        Scene& scene = m_deps.scene;
        const String module = scene.Components<ScriptComponent>().Find(node)->module;
        const auto defaults = scripts.defaults ? scripts.defaults(module) : EngineResult<ScriptProps> {};
        if (!defaults) {
            ImGui::PushStyleColor(ImGuiCol_Text, ScriptErrorColor);
            ImGui::TextWrapped("%s", defaults.error().message.c_str());
            ImGui::PopStyleColor();
            if (ImGui::SmallButton(RetryButton) && scripts.forgetDefaults)
                scripts.forgetDefaults();
            return;
        }
        if (defaults->empty())
            ImGui::TextDisabled("The script has no props");
        else if (ui::BeginPropertyGrid()) {
            for (const auto& [name, fallback] : *defaults)
                DrawScriptProp(node, name, fallback);
            ui::EndPropertyGrid();
        }
        if (ScriptComponent* script = scene.Components<ScriptComponent>().Find(node))
            DrawUnknownProps(*script, *defaults);
    }

    void NodeInspectorWindow::DrawScriptProp(NodeId node, const String& name, const ScriptValue& fallback)
    {
        Scene& scene = m_deps.scene;
        const ScriptInspection& scripts = m_deps.scripts;
        // Reading the defaults ran the module's code, which may change the scene: the component is looked up each time
        ScriptComponent* script = scene.Components<ScriptComponent>().Find(node);
        if (!script)
            return;
        const ScriptValue* overridden = FindOverride(*script, name);
        ScriptValue value = overridden ? *overridden : fallback;
        const bool isOverridden = overridden != nullptr;

        ImGui::PushID(name.c_str());
        if (isOverridden)
            ImGui::PushStyleColor(ImGuiCol_Text, OverrideColor);
        ui::PropertyRow(name.c_str());
        if (isOverridden) {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float resetWidth = SmallButtonWidth(ResetPropButton) + style.ItemSpacing.x;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - resetWidth);
        }
        const bool changed = DrawScriptValue(value, scene);
        if (isOverridden)
            ImGui::PopStyleColor();
        if (changed) {
            SetOverride(*script, name, value);
            if (scripts.setField)
                scripts.setField(node, name, value);
        }
        if (isOverridden) {
            ImGui::SameLine();
            if (ImGui::SmallButton(ResetPropButton)) {
                RemoveOverride(*script, name);
                if (scripts.setField)
                    scripts.setField(node, name, fallback);
            }
            ImGui::SetItemTooltip("Take the value of the script again");
        }
        ImGui::PopID();
    }

    void NodeInspectorWindow::DrawUnknownProps(ScriptComponent& script, const ScriptProps& props)
    {
        // Values for props the script does not have (any more): they would fail the instance
        for (size_t index = 0; index < script.props.size();) {
            const String name = script.props[index].first;
            const bool known = std::any_of(props.begin(), props.end(), [&](const auto& prop) { return prop.first == name; });
            if (known) {
                ++index;
                continue;
            }
            ImGui::PushID(name.c_str());
            ImGui::TextColored(ScriptErrorColor, "%s: not a prop of the script", name.c_str());
            ImGui::SameLine();
            const bool remove = ImGui::SmallButton(RemovePropButton);
            ImGui::SetItemTooltip("Remove the value");
            ImGui::PopID();
            if (remove)
                script.props.erase(script.props.begin() + static_cast<std::ptrdiff_t>(index));
            else
                ++index;
        }
    }

    void NodeInspectorWindow::DrawAddComponent(NodeId node)
    {
        Scene& scene = m_deps.scene;
        ImGui::Spacing();
        if (ImGui::Button(AddComponentButton, ImVec2(-FLT_MIN, 0.0f))) {
            m_scriptModules = ListScriptModules(m_folders.scripts);
            ImGui::OpenPopup("AddComponentPopup");
        }
        if (!ImGui::BeginPopup("AddComponentPopup"))
            return;
        if (ImGui::BeginMenu(ComponentLabel(ComponentType::Model).c_str(), !scene.Components<ModelComponent>().Contains(node))) {
            DrawAddModelMenu(node);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ComponentLabel(ComponentType::RigidBody).c_str(), nullptr, false, !scene.Components<RigidBody>().Contains(node)))
            scene.Components<RigidBody>().Emplace(node, NewRigidBody());
        if (ImGui::MenuItem(ComponentLabel(ComponentType::Collider).c_str(), nullptr, false, !scene.Components<Collider>().Contains(node)))
            scene.Components<Collider>().Emplace(node, FitColliderToModel(FindNodeModel(scene, m_deps.models, node)));
        if (ImGui::BeginMenu(ComponentLabel(ComponentType::Script).c_str(), !scene.Components<ScriptComponent>().Contains(node))) {
            DrawAddScriptMenu(node);
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    void NodeInspectorWindow::DrawAddModelMenu(NodeId node)
    {
        Scene& scene = m_deps.scene;
        ModelCatalog& models = m_deps.models;
        auto addComponent = [&](ModelHandler handle) {
            scene.Components<ModelComponent>().Emplace(node, ModelComponent { .model = handle, .color = DefaultModelColor, .visible = true });
        };
        if (ImGui::MenuItem("Browse from Disk..."))
            OpenModelDialog(node);
        for (const ModelPrimitive primitive : ModelPrimitives) {
            if (ImGui::MenuItem(ModelPrimitiveName(primitive)))
                addComponent(models.Insert(MakePrimitiveModel(primitive)));
        }
        if (models.Size() == 0)
            return;
        ImGui::Separator();
        ImGui::TextDisabled("Loaded Models:");
        models.ForEach([&](ModelHandler handle, const Model& model) {
            ImGui::PushID(static_cast<int>(handle.Raw()));
            if (ImGui::MenuItem(FormatModelLabel(&model, handle).c_str()))
                addComponent(handle);
            ImGui::PopID();
        });
    }

    void NodeInspectorWindow::DrawAddScriptMenu(NodeId node)
    {
        auto addComponent = [&](const String& module) {
            m_deps.scene.Components<ScriptComponent>().Emplace(node, ScriptComponent { .module = module, .props = {} });
        };
        if (ImGui::MenuItem("Empty Script Component"))
            addComponent(String {});
        ImGui::Separator();
        for (const auto& module : m_scriptModules) {
            if (ImGui::MenuItem(module.c_str()))
                addComponent(module);
        }
    }

} // namespace elm
