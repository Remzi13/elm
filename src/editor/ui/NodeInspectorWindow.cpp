#include "editor/ui/NodeInspectorWindow.hpp"

#include "editor/NodeEditing.hpp"
#include "editor/SceneDocument.hpp"
#include "editor/ui/EditorWidgets.hpp"
#include "editor/ui/SceneEditShortcuts.hpp"

#include "graphics/gizmo/GizmoSettings.hpp"

#include "math/Matrix.hpp"
#include "math/Rotation.hpp"

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
        constexpr const char* SectionMenuButton = "...";
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

        float SmallButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }

        /// The header of a section with room for what lies on it and the button of its menu at the right edge.
        bool DrawSectionHeader(const char* title)
        {
            const bool open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - SmallButtonWidth(SectionMenuButton));
            if (ImGui::SmallButton(SectionMenuButton))
                ImGui::OpenPopup(SectionMenuPopup);
            return open;
        }

        /// The menu of a component's section: its own items, then Reset and Remove.
        template <typename OwnItems>
        ComponentAction DrawComponentMenu(const OwnItems& ownItems)
        {
            ComponentAction action = ComponentAction::None;
            if (!ImGui::BeginPopup(SectionMenuPopup))
                return action;
            ownItems();
            if (ImGui::MenuItem("Reset"))
                action = ComponentAction::Reset;
            if (ImGui::MenuItem("Remove"))
                action = ComponentAction::Remove;
            ImGui::EndPopup();
            return action;
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
        if (!ui::BeginPropertyGrid())
            return;
        ui::PropertyRow("Name");
        String name = m_deps.scene.Find(node)->name;
        if (ui::InputString("##Name", name))
            (void)m_deps.scene.SetName(node, name);
        ui::PropertyRow("Parent");
        DrawParentCombo(node);
        ui::EndPropertyGrid();
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
        const bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        // Which space the values are in, then the menu
        const ImGuiStyle& style = ImGui::GetStyle();
        const float menuX = ImGui::GetContentRegionMax().x - SmallButtonWidth(SectionMenuButton);
        const float worldX = menuX - style.ItemSpacing.x - SmallButtonWidth("World");
        const float localX = worldX - style.ItemSpacing.x - SmallButtonWidth("Local");
        ImGui::SameLine(localX);
        if (ui::SmallToggleButton("Local", m_editLocalTransform))
            m_editLocalTransform = true;
        ImGui::SameLine(worldX);
        if (ui::SmallToggleButton("World", !m_editLocalTransform))
            m_editLocalTransform = false;
        ImGui::SameLine(menuX);
        if (ImGui::SmallButton(SectionMenuButton))
            ImGui::OpenPopup(SectionMenuPopup);

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

        DrawGizmoRow();
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

    void NodeInspectorWindow::DrawGizmoRow()
    {
        GizmoSettings& gizmo = m_deps.gizmo;
        constexpr struct {
            const char* label;
            const char* tooltip;
            GizmoOperation operation;
        } Operations[] {
            { "Move", "The gizmo moves the node (W in the viewport)", GizmoOperation::Translate },
            { "Rotate", "The gizmo rotates the node (E in the viewport)", GizmoOperation::Rotate },
            { "Scale", "The gizmo scales the node, always along its own axes (R in the viewport)", GizmoOperation::Scale },
        };
        ui::PropertyRow("Gizmo");
        for (const auto& item : Operations) {
            if (ui::ToggleButton(item.label, gizmo.operation == item.operation))
                gizmo.operation = item.operation;
            ImGui::SetItemTooltip("%s", item.tooltip);
            ImGui::SameLine();
        }
        const bool world = gizmo.space == GizmoSpace::World;
        if (ImGui::Button(world ? "World###GizmoSpace" : "Local###GizmoSpace"))
            gizmo.space = world ? GizmoSpace::Local : GizmoSpace::World;
        ImGui::SetItemTooltip("The axes of the gizmo: click to switch. The values below have their own switch in the header.");
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
        const bool open = DrawSectionHeader("Model");
        const ComponentAction action = DrawComponentMenu([&] {
            if (ImGui::BeginMenu("Set Primitive")) {
                for (const ModelPrimitive primitive : ModelPrimitives) {
                    if (ImGui::MenuItem(ModelPrimitiveName(primitive)))
                        component->model = m_deps.models.Insert(MakePrimitiveModel(primitive));
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Open in Model Editor", nullptr, false, m_deps.models.Get(component->model) != nullptr) && m_deps.files.sceneModel)
                m_deps.files.sceneModel(component->model);
        });
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
        constexpr const char* BrowseLabel = "Browse...";
        const ImGuiStyle& style = ImGui::GetStyle();
        const float browseWidth = ImGui::CalcTextSize(BrowseLabel).x + style.FramePadding.x * 2.0f;
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
        if (ImGui::Button(BrowseLabel))
            OpenModelDialog(node);

        ui::PropertyRow("Color");
        ImGui::ColorEdit4("##Color", &component.color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ui::PropertyRow("Visible");
        ImGui::Checkbox("##Visible", &component.visible);
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
        const bool open = DrawSectionHeader("RigidBody");
        const ComponentAction action = DrawComponentMenu([] {});
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
            if (ImGui::SmallButton("+ Add Collider"))
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
        const bool open = DrawSectionHeader("Collider");
        const ComponentAction action = DrawComponentMenu([&] {
            if (ImGui::MenuItem("Fit to Model", nullptr, false, model != nullptr)) {
                Collider fitted = FitColliderToModel(model);
                fitted.shape = collider->shape;
                fitted.trigger = collider->trigger;
                *collider = fitted;
            }
        });
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
            if (ImGui::SmallButton("+ Add RigidBody"))
                scene.Components<RigidBody>().Emplace(node, NewRigidBody());
        }
    }

    void NodeInspectorWindow::DrawScriptSection(NodeId node)
    {
        Scene& scene = m_deps.scene;
        if (!scene.Components<ScriptComponent>().Find(node))
            return;
        ImGui::PushID("Script");
        const bool open = DrawSectionHeader("Script");
        const ComponentAction action = DrawComponentMenu([] {});
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
        constexpr const char* PickLabel = "...##PickModule";
        const float pickWidth = ImGui::CalcTextSize("...").x + style.FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - pickWidth - style.ItemSpacing.x);
        String module = script.module;
        if (ui::InputString("##Module", module))
            script.module = module;
        ImGui::SameLine();
        if (ImGui::Button(PickLabel)) {
            m_scriptModules = ListScriptModules(m_folders.scripts);
            m_scriptFilter[0] = '\0';
            ImGui::OpenPopup("PickModulePopup");
        }
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
            if (ImGui::SmallButton("Reload module") && scripts.reloadModule)
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
            if (ImGui::SmallButton("Try again") && scripts.forgetDefaults)
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
        constexpr const char* ResetLabel = "Reset";
        if (isOverridden) {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float resetWidth = SmallButtonWidth(ResetLabel) + style.ItemSpacing.x;
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
            if (ImGui::SmallButton(ResetLabel)) {
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
            const bool remove = ImGui::SmallButton("Remove");
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
        if (ImGui::Button("+ Add Component", ImVec2(-FLT_MIN, 0.0f))) {
            m_scriptModules = ListScriptModules(m_folders.scripts);
            ImGui::OpenPopup("AddComponentPopup");
        }
        if (!ImGui::BeginPopup("AddComponentPopup"))
            return;
        if (ImGui::BeginMenu("Model", !scene.Components<ModelComponent>().Contains(node))) {
            DrawAddModelMenu(node);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("RigidBody", nullptr, false, !scene.Components<RigidBody>().Contains(node)))
            scene.Components<RigidBody>().Emplace(node, NewRigidBody());
        if (ImGui::MenuItem("Collider", nullptr, false, !scene.Components<Collider>().Contains(node)))
            scene.Components<Collider>().Emplace(node, FitColliderToModel(FindNodeModel(scene, m_deps.models, node)));
        if (ImGui::BeginMenu("Script", !scene.Components<ScriptComponent>().Contains(node))) {
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
