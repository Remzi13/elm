#include "graphics/ui/SceneHierarchyWindow.hpp"

#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include "core/Unexpected.hpp"
#include "math/Matrix.hpp"
#include "math/Primitivs.hpp"
#include "math/Rotation.hpp"
#include "math/Vector.hpp"
#include "render/RenderSystem.hpp"
#include "resmgr/AssetImport.hpp"
#include "resmgr/GltfExporter.hpp"
#include "resmgr/Serializer.hpp"
#include "Scene/ComponentTypes.hpp"
#include "Scene/ModelComponent.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/ScriptComponent.hpp"
#include "Scene/TestScenes.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace elm {
namespace {

constexpr float Rad2Deg = 180.0f / 3.14159265358979323846f;
constexpr float Deg2Rad = 3.14159265358979323846f / 180.0f;

int ResizeInputBuffer(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
        return 0;

    auto& buffer = *static_cast<Vector<char>*>(data->UserData);
    buffer.resize(static_cast<size_t>(data->BufTextLen) + 1);
    data->Buf = buffer.data();
    return 0;
}

bool InputString(const char* label, String& text, ImGuiInputTextFlags flags = 0)
{
    Vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0');
    if (!ImGui::InputText(label, buffer.data(), buffer.size(), flags | ImGuiInputTextFlags_CallbackResize, ResizeInputBuffer, &buffer))
        return false;
    text.assign(buffer.data(), std::strlen(buffer.data()));
    return true;
}

bool ContainsNoCase(StringView text, StringView part)
{
    if (part.empty())
        return true;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(text.begin(), text.end(), part.begin(), part.end(),
        [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
}

String NodeDisplayName(const Scene::Node& node, size_t index)
{
    if (!node.name.empty())
        return node.name;
    return String("Node ") + std::to_string(index).c_str();
}

String FormatModelLabel(const graphics::Model* model, graphics::ModelHandler handle)
{
    if (!model)
        return String("No Model");
    String name = model->name.empty() ? (String("Model #") + std::to_string(handle.Raw()).c_str()) : model->name;
    name += " (";
    name += std::to_string(model->submeshes.size()).c_str();
    name += " submeshes)";
    return name;
}

bool NodeMatchesFilter(const Scene& scene, size_t nodeIndex, const Vector<Vector<size_t>>& nodeChildren, const char* filter)
{
    if (!filter || filter[0] == '\0')
        return true;
    const auto& node = scene.Nodes()[nodeIndex];
    if (ContainsNoCase(node.name, filter))
        return true;
    for (const size_t childIndex : nodeChildren[nodeIndex]) {
        if (NodeMatchesFilter(scene, childIndex, nodeChildren, filter))
            return true;
    }
    return false;
}

Collider FitColliderToModel(const graphics::Model* model)
{
    Collider collider;
    if (!model || model->submeshes.empty())
        return collider;
    const auto bounds = model->GetLocalBounds();
    if (bounds.minBounds.x > bounds.maxBounds.x)
        return collider;

    math::Vector3 half = (bounds.maxBounds - bounds.minBounds) * 0.5f;
    math::Vector3 center = (bounds.maxBounds + bounds.minBounds) * 0.5f;
    constexpr float FlatThickness = 0.25f;
    if (half.x < 0.01f) { half.x = FlatThickness; center.x -= FlatThickness; }
    if (half.y < 0.01f) { half.y = FlatThickness; center.y -= FlatThickness; }
    if (half.z < 0.01f) { half.z = FlatThickness; center.z -= FlatThickness; }

    collider.halfExtents = half;
    collider.offset = center;
    collider.radius = (std::max)(half.x, half.z);
    collider.halfHeight = (std::max)(half.y - collider.radius, 0.0f);
    return collider;
}

const ImVec4 ScriptErrorColor { 1.0f, 0.40f, 0.36f, 1.0f };
const ImVec4 ScriptRunningColor { 0.40f, 0.85f, 0.50f, 1.0f };
/// Props whose value is set on this node, not taken from the script.
const ImVec4 OverrideColor { 0.55f, 0.75f, 1.0f, 1.0f };

const ScriptValue* FindOverride(const ScriptComponent& script, StringView name)
{
    for (const auto& [key, value] : script.props) {
        if (key == name)
            return &value;
    }
    return nullptr;
}

/// Props stay sorted by name, as they come from Lua and from scene files.
void SetOverride(ScriptComponent& script, const String& name, ScriptValue value)
{
    const auto position = std::lower_bound(script.props.begin(), script.props.end(), name,
        [](const auto& prop, const String& key) { return prop.first < key; });
    if (position != script.props.end() && position->first == name)
        position->second = std::move(value);
    else
        script.props.emplace(position, name, std::move(value));
}

void RemoveOverride(ScriptComponent& script, StringView name)
{
    std::erase_if(script.props, [&](const auto& prop) { return prop.first == name; });
}

bool DrawScriptValue(const char* label, ScriptValue& value, Scene& scene)
{
    return std::visit([&]<typename V>(V& item) -> bool {
        if constexpr (std::is_same_v<V, bool>) {
            return ImGui::Checkbox(label, &item);
        } else if constexpr (std::is_same_v<V, int64_t>) {
            return ImGui::DragScalar(label, ImGuiDataType_S64, &item, 0.2f);
        } else if constexpr (std::is_same_v<V, double>) {
            return ImGui::DragScalar(label, ImGuiDataType_Double, &item, 0.02f, nullptr, nullptr, "%.3f");
        } else if constexpr (std::is_same_v<V, String>) {
            return InputString(label, item);
        } else if constexpr (std::is_same_v<V, math::Vector3>) {
            return ImGui::DragFloat3(label, &item.x, 0.05f);
        } else if constexpr (std::is_same_v<V, math::Vector4>) {
            return ImGui::ColorEdit4(label, &item.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        } else if constexpr (std::is_same_v<V, NodeId>) {
            const Scene::Node* target = scene.Find(item);
            String preview = target ? (target->name.empty() ? String("Unnamed Node") : target->name) : (item ? String("(Deleted)") : String("None"));
            bool changed = false;
            if (ImGui::BeginCombo(label, preview.c_str())) {
                if (ImGui::Selectable("None", !item)) {
                    item = NodeId { };
                    changed = true;
                }
                for (const auto& node : scene.Nodes()) {
                    String nodeName = NodeDisplayName(node, scene.IndexOf(node.id));
                    ImGui::PushID(static_cast<int>(node.id.Raw()));
                    if (ImGui::Selectable(nodeName.c_str(), item == node.id)) {
                        item = node.id;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }
    }, value);
}

} // namespace

SceneHierarchyWindow::SceneHierarchyWindow(Settings& settings, Folders folders, const Path& openedScene)
    : IImGuiWindow(settings)
    , m_folders(std::move(folders))
{
    // Never a default name like scene.scene: Save would overwrite a scene the user did not open
    ShowSceneFile(openedScene.Empty() ? m_folders.scenes / UnusedSceneFileName() : openedScene);
}

void SceneHierarchyWindow::OnAttach()
{
    m_querySubscriptions.push_back(RegisterQuery<SelectedSceneInstance>([this] {
        return SelectedSceneInstance { m_selectedNode };
    }));
    m_querySubscriptions.push_back(RegisterQuery<OpenSceneFile>([this] {
        return OpenSceneFile { [this](const Path& file) {
            m_requestedScene = file;
            m_visible = true;
        } };
    }));
    m_querySubscriptions.push_back(RegisterQuery<GizmoSettings>([this] {
        return GizmoSettings {
            m_gizmoOperation,
            m_useLocalSpace ? GizmoSpace::Local : GizmoSpace::World
        };
    }));
}

void SceneHierarchyWindow::Render(ImGuiWindowContext& context)
{
    if (!m_visible)
        return;

    ImGui::SetNextWindowSize(ImVec2(800.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(GetName().data(), &m_visible)) {
        ImGui::End();
        return;
    }

    if (!m_scriptModulesLoaded)
        RefreshScriptModules();
    if (m_requestedScene) {
        const Path file = std::move(*m_requestedScene);
        m_requestedScene.reset();
        LoadSceneFile(context, file);
    }

    auto& scene = context.scene;
    if (!scene.Find(m_selectedNode))
        m_selectedNode = scene.IdOf(0);

    DrawToolbar(context);

    ImGui::Separator();

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    m_hierarchyWidth = (std::clamp)(m_hierarchyWidth, 160.0f, (std::max)(160.0f, availableWidth - 200.0f));

    DrawHierarchyTree(context);

    ImGui::SameLine();

    // Resizable splitter bar between hierarchy and details
    ImGui::InvisibleButton("##SceneHierarchySplitter", ImVec2(6.0f, ImGui::GetContentRegionAvail().y));
    if (ImGui::IsItemActive()) {
        m_hierarchyWidth += ImGui::GetIO().MouseDelta.x;
        m_hierarchyWidth = (std::clamp)(m_hierarchyWidth, 160.0f, (std::max)(160.0f, availableWidth - 200.0f));
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(min.x + 1.0f, min.y),
            ImVec2(max.x - 1.0f, max.y),
            ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered)
        );
    }
    ImGui::SameLine();

    ImGui::BeginChild("NodeDetails", ImVec2(0.0f, 0.0f), true);
    DrawNodeDetails(context, m_selectedNode);
    ImGui::EndChild();

    ImGui::End();
}

void SceneHierarchyWindow::DrawToolbar(ImGuiWindowContext& context)
{
    auto& scene = context.scene;

    if (const auto play = Request<PlayControls>(); play && play->playing) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.3f, 1.0f));
        ImGui::TextUnformatted("[Play Mode Active] Scene is simulated. Stop first to save or apply permanent edits.");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    ImGui::TextUnformatted("Scene File:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("##SceneFile", m_sceneFilePath, sizeof(m_sceneFilePath));
    ImGui::SameLine();

    const Path scenePath = Path(m_sceneFilePath).IsAbsolute() ? Path(m_sceneFilePath) : m_folders.scenes / m_sceneFilePath;
    if (ImGui::Button("Save")) {
        if (const auto play = Request<PlayControls>(); play && play->playing) {
            m_sceneFileStatus = "Stop first: in Play mode the scene is the running game.";
        } else {
            auto result = SaveScene(context, scenePath);
            m_sceneFileStatus = result ? "Scene saved to " + String(scenePath.string().c_str()) + "." : result.error().message;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) {
        m_sceneSaveFileDialog.SaveAs("Save Scene", ".scene", m_sceneFilePath, m_folders.scenes);
        m_sceneSaveFileDialog.LimitTo(m_folders.assets);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load"))
        LoadSceneFile(context, scenePath);
    ImGui::SameLine();
    if (ImGui::Button("Open...")) {
        m_sceneOpenFileDialog.Open("Open Scene", ".scene", m_folders.scenes);
        m_sceneOpenFileDialog.LimitTo(m_folders.assets);
    }
    ImGui::SameLine();
    if (ImGui::Button("Export glTF...")) {
        m_gltfFileDialog.SaveAs("Export Scene to glTF", ".gltf", "scene.gltf", m_folders.scenes);
    }
    ImGui::SameLine();
    if (ImGui::Button("Import glTF...")) {
        m_gltfSceneImportFileDialog.Open("Import glTF Scene", ".gltf;.glb", m_folders.models);
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        scene.Clear();
        context.models.Clear();
        m_selectedNode = NodeId { };
        // A name no scene has yet: Save does not overwrite the scene that was open
        const String fileName = UnusedSceneFileName();
        std::snprintf(m_sceneFilePath, sizeof(m_sceneFilePath), "%s", fileName.c_str());
        m_sceneFileStatus = "New scene " + fileName + ".";
    }

    if (const auto path = m_sceneSaveFileDialog.Draw()) {
        if (const auto play = Request<PlayControls>(); play && play->playing) {
            m_sceneFileStatus = "Stop first: in Play mode the scene is the running game.";
        } else {
            auto result = SaveScene(context, *path);
            if (result) {
                m_sceneFileStatus = "Scene saved to " + String(path->string().c_str()) + ".";
                std::snprintf(m_sceneFilePath, sizeof(m_sceneFilePath), "%s", path->Filename().c_str());
            } else {
                m_sceneFileStatus = result.error().message;
            }
        }
    }

    if (const auto path = m_sceneOpenFileDialog.Draw())
        LoadSceneFile(context, *path);

    if (const auto path = m_gltfFileDialog.Draw()) {
        auto result = resmgr::GltfExporter::Export(*path, scene, context.models);
        m_sceneFileStatus = result ? "Scene exported to " + String(path->string().c_str()) + "." : result.error().message;
    }

    // Handle deferred file dialog opens from DrawHierarchyTree popup
    if (m_deferModelOpen) {
        m_deferModelOpen = false;
        m_modelOpenFileDialog.Open("Select Model File", ".model;.gltf;.glb", m_folders.models);
    }
    if (m_deferGltfSceneImport) {
        m_deferGltfSceneImport = false;
        m_gltfSceneImportFileDialog.Open("Import glTF Scene", ".gltf;.glb", m_folders.models);
    }

    if (const auto path = m_gltfSceneImportFileDialog.Draw()) {
        // The original goes to the project's source folder, the models to its models folder
        auto result = resmgr::AssetImport::ImportScene(*path, m_folders.assets);
        if (result) {
            const auto handlers = context.models.InsertAll(std::move(result->models), result->modelFiles);
            const auto newIds = scene.Append(std::move(*result), handlers);
            if (!newIds.empty())
                m_selectedNode = newIds.front();
            m_sceneFileStatus = "glTF scene imported from " + String(path->Filename().c_str()) + ".";
        } else {
            m_sceneFileStatus = "Failed to import glTF scene: " + result.error().message;
        }
    }

    if (const auto path = m_modelOpenFileDialog.Draw()) {
        // A .model of the project stays its file; anything else is imported into the project
        auto modelResult = resmgr::AssetImport::ImportModel(*path, m_folders.assets);
        if (!modelResult) {
            m_sceneFileStatus = "Failed to load model: " + modelResult.error().message;
        } else {
            const String modelName = modelResult->model.name;
            const auto handle = context.models.Insert(std::move(modelResult->model), std::move(modelResult->file));

            if (m_modelDialogAction == ModelDialogAction::AssignToComponent) {
                if (auto* comp = scene.Components<ModelComponent>().Find(m_modelDialogTargetNode)) {
                    comp->model = handle;
                    if (const auto* node = scene.Find(m_modelDialogTargetNode)) {
                        if (node->name.empty() || node->name == "Cube" || node->name == "Empty Node" || node->name == "Node" || node->name == "Model")
                            scene.SetName(m_modelDialogTargetNode, modelName);
                    }
                    m_sceneFileStatus = "Model assigned from " + String(path->Filename().c_str()) + ".";
                }
            } else if (m_modelDialogAction == ModelDialogAction::AddNewRootNode || m_modelDialogAction == ModelDialogAction::AddNewChildNode) {
                const NodeId parentId = (m_modelDialogAction == ModelDialogAction::AddNewChildNode) ? m_modelDialogTargetNode : NodeId { };
                const NodeId newNode = scene.AddNode(Scene::NodeDesc {
                    .name = modelName,
                    .parent = parentId,
                    .localTransform = math::Matrix4x4::Identity(),
                });
                scene.Components<ModelComponent>().Emplace(newNode, ModelComponent {
                    .model = handle,
                    .color = math::Vector4 { 0.8f, 0.8f, 0.8f, 1.0f },
                    .visible = true,
                });
                m_selectedNode = newNode;
                m_sceneFileStatus = "Added node \"" + modelName + "\" with model from " + String(path->Filename().c_str()) + ".";
            }
        }
    }

    if (!m_sceneFileStatus.empty()) {
        ImVec4 statusColor(0.7f, 0.7f, 0.75f, 1.0f);
        if (m_sceneFileStatus.starts_with("Scene saved") || m_sceneFileStatus.starts_with("Scene loaded") || m_sceneFileStatus.starts_with("Scene exported"))
            statusColor = ImVec4(0.4f, 0.85f, 0.5f, 1.0f);
        else if (m_sceneFileStatus.starts_with("Stop") || m_sceneFileStatus.starts_with("Cannot") || m_sceneFileStatus.starts_with("Error") || m_sceneFileStatus.starts_with("Failed"))
            statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        ImGui::TextColored(statusColor, "%s", m_sceneFileStatus.c_str());
    }
}

void SceneHierarchyWindow::DrawHierarchyTree(ImGuiWindowContext& context)
{
    auto& scene = context.scene;
    ImGui::BeginChild("SceneObjects", ImVec2(m_hierarchyWidth, 0.0f), true);

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##TreeFilter", "Search objects...", m_hierarchyFilter, sizeof(m_hierarchyFilter));

    if (ImGui::Button("+ Add Object..."))
        ImGui::OpenPopup("AddObjectPopup");

    if (ImGui::BeginPopup("AddObjectPopup")) {
        if (ImGui::MenuItem("Empty Node"))
            m_selectedNode = AddPrimitiveNode(scene, context.models, NodeId { }, "Empty Node", 0);

        if (ImGui::MenuItem("Model from Disk (.gltf, .glb, .model)...")) {
            m_modelDialogAction = ModelDialogAction::AddNewRootNode;
            m_modelDialogTargetNode = NodeId { };
            m_deferModelOpen = true;
        }

        if (context.models.Size() > 0 && ImGui::BeginMenu("From Loaded Model")) {
            context.models.ForEach([&](graphics::ModelHandler handle, const graphics::Model& m) {
                String label = FormatModelLabel(&m, handle);
                if (ImGui::MenuItem(label.c_str())) {
                    String name = m.name.empty() ? "Model" : m.name;
                    const NodeId id = scene.AddNode({ .name = name, .parent = NodeId { }, .localTransform = math::Matrix4x4::Identity() });
                    scene.Components<ModelComponent>().Emplace(id, ModelComponent {
                        .model = handle,
                        .color = math::Vector4 { 0.8f, 0.8f, 0.8f, 1.0f },
                        .visible = true,
                    });
                    m_selectedNode = id;
                }
            });
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("3D Primitives")) {
            if (ImGui::MenuItem("Cube"))
                m_selectedNode = AddPrimitiveNode(scene, context.models, NodeId { }, "Cube", 1);
            if (ImGui::MenuItem("Wall"))
                m_selectedNode = AddPrimitiveNode(scene, context.models, NodeId { }, "Wall", 2);
            if (ImGui::MenuItem("Ground Plane"))
                m_selectedNode = AddPrimitiveNode(scene, context.models, NodeId { }, "Ground Plane", 3);
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Import glTF Scene Hierarchy...")) {
            m_deferGltfSceneImport = true;
        }

        if (m_selectedNode && scene.Find(m_selectedNode)) {
            ImGui::Separator();
            ImGui::TextDisabled("As Child of Selected:");
            if (ImGui::MenuItem("Empty Child Node"))
                m_selectedNode = AddPrimitiveNode(scene, context.models, m_selectedNode, "Empty Child", 0);

            if (ImGui::MenuItem("Child Model from Disk...")) {
                m_modelDialogAction = ModelDialogAction::AddNewChildNode;
                m_modelDialogTargetNode = m_selectedNode;
                m_deferModelOpen = true;
            }

            if (context.models.Size() > 0 && ImGui::BeginMenu("From Loaded Model (as Child)")) {
                context.models.ForEach([&](graphics::ModelHandler handle, const graphics::Model& m) {
                    String label = FormatModelLabel(&m, handle);
                    if (ImGui::MenuItem(label.c_str())) {
                        String name = m.name.empty() ? "Model" : m.name;
                        const NodeId id = scene.AddNode({ .name = name, .parent = m_selectedNode, .localTransform = math::Matrix4x4::Identity() });
                        scene.Components<ModelComponent>().Emplace(id, ModelComponent {
                            .model = handle,
                            .color = math::Vector4 { 0.8f, 0.8f, 0.8f, 1.0f },
                            .visible = true,
                        });
                        m_selectedNode = id;
                    }
                });
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("3D Primitives (as Child)")) {
                if (ImGui::MenuItem("Cube Child"))
                    m_selectedNode = AddPrimitiveNode(scene, context.models, m_selectedNode, "Cube", 1);
                if (ImGui::MenuItem("Wall Child"))
                    m_selectedNode = AddPrimitiveNode(scene, context.models, m_selectedNode, "Wall", 2);
                if (ImGui::MenuItem("Ground Plane Child"))
                    m_selectedNode = AddPrimitiveNode(scene, context.models, m_selectedNode, "Ground Plane", 3);
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate Selected"))
                m_selectedNode = DuplicateNode(scene, m_selectedNode);
        }
        ImGui::EndPopup();
    }


    ImGui::SameLine();
    ImGui::BeginDisabled(!m_selectedNode || !scene.Find(m_selectedNode));
    if (ImGui::Button("Duplicate"))
        m_selectedNode = DuplicateNode(scene, m_selectedNode);
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        const size_t removedIndex = scene.IndexOf(m_selectedNode);
        scene.RemoveNode(m_selectedNode);
        m_selectedNode = scene.IdOf((std::min)(removedIndex, scene.NodeCount() - 1));
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    const auto nodes = scene.Nodes();
    Vector<Vector<size_t>> nodeChildren(nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const size_t parentIndex = scene.IndexOf(nodes[nodeIndex].parent);
        if (parentIndex != Scene::NoNode)
            nodeChildren[parentIndex].push_back(nodeIndex);
    }

    ImGui::BeginChild("TreeScrollArea", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), false);
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        if (!nodes[nodeIndex].parent)
            DrawNodeItem(context, nodeIndex, nodeChildren);
    }

    // Context menu in empty space
    if (ImGui::BeginPopupContextWindow("TreeEmptyAreaContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Add Empty Node"))
            m_selectedNode = AddPrimitiveNode(scene, context.models, NodeId { }, "Empty Node", 0);
        if (ImGui::MenuItem("Add Model from Disk...")) {
            m_modelDialogAction = ModelDialogAction::AddNewRootNode;
            m_modelDialogTargetNode = NodeId { };
            m_modelOpenFileDialog.Open("Select Model File", ".model;.gltf;.glb", m_folders.models);
        }
        if (context.models.Size() > 0 && ImGui::BeginMenu("Add from Loaded Model")) {
            context.models.ForEach([&](graphics::ModelHandler handle, const graphics::Model& m) {
                String label = FormatModelLabel(&m, handle);
                if (ImGui::MenuItem(label.c_str())) {
                    String name = m.name.empty() ? "Model" : m.name;
                    const NodeId id = scene.AddNode({ .name = name, .parent = NodeId { }, .localTransform = math::Matrix4x4::Identity() });
                    scene.Components<ModelComponent>().Emplace(id, ModelComponent {
                        .model = handle,
                        .color = math::Vector4 { 0.8f, 0.8f, 0.8f, 1.0f },
                        .visible = true,
                    });
                    m_selectedNode = id;
                }
            });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add 3D Primitive")) {
            if (ImGui::MenuItem("Cube"))
                m_selectedNode = AddPrimitiveNode(scene, context.models, NodeId { }, "Cube", 1);
            if (ImGui::MenuItem("Wall"))
                m_selectedNode = AddPrimitiveNode(scene, context.models, NodeId { }, "Wall", 2);
            if (ImGui::MenuItem("Ground Plane"))
                m_selectedNode = AddPrimitiveNode(scene, context.models, NodeId { }, "Ground Plane", 3);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Import glTF Scene Hierarchy...")) {
            m_gltfSceneImportFileDialog.Open("Import glTF Scene", ".gltf;.glb", m_folders.models);
        }
        ImGui::EndPopup();
    }

    // Dropping onto empty space makes the dragged node root
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_NODE_DRAG")) {
            NodeId draggedId;
            std::memcpy(&draggedId, payload->Data, sizeof(NodeId));
            if (draggedId) {
                (void)scene.SetParent(draggedId, NodeId { });
                m_sceneFileStatus = "Node moved to scene root.";
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::EndChild();

    ImGui::TextDisabled("Objects: %zu", scene.NodeCount());
    ImGui::EndChild();
    // After EndChild the tree is the last item: it is where models are dropped
    AcceptDroppedModel(context);
}

void SceneHierarchyWindow::DrawNodeItem(ImGuiWindowContext& context, size_t nodeIndex, const Vector<Vector<size_t>>& nodeChildren)
{
    auto& scene = context.scene;
    const auto nodes = scene.Nodes();
    if (nodeIndex >= nodes.size())
        return;

    if (!NodeMatchesFilter(scene, nodeIndex, nodeChildren, m_hierarchyFilter))
        return;

    const auto& node = nodes[nodeIndex];
    String label = NodeDisplayName(node, nodeIndex);

    const bool hasM = scene.FindComponents<ModelComponent>() && scene.FindComponents<ModelComponent>()->Contains(node.id);
    const bool hasRB = scene.FindComponents<RigidBody>() && scene.FindComponents<RigidBody>()->Contains(node.id);
    const bool hasCol = scene.FindComponents<Collider>() && scene.FindComponents<Collider>()->Contains(node.id);
    const bool hasS = scene.FindComponents<ScriptComponent>() && scene.FindComponents<ScriptComponent>()->Contains(node.id);

    if (hasM || hasRB || hasCol || hasS) {
        label += " [";
        if (hasM) label += "M";
        if (hasRB) label += "P";
        if (hasCol) label += "C";
        if (hasS) label += "S";
        label += "]";
    }
    label += "##node";
    label += std::to_string(node.id.Raw()).c_str();

    const bool hasChildren = !nodeChildren[nodeIndex].empty();
    const bool isSelected = (m_selectedNode == node.id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (m_hierarchyFilter[0] != '\0')
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    const bool isOpen = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        m_selectedNode = node.id;

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("SCENE_NODE_DRAG", &node.id, sizeof(NodeId));
        ImGui::Text("Move %s", node.name.empty() ? "Node" : node.name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_NODE_DRAG")) {
            NodeId draggedId;
            std::memcpy(&draggedId, payload->Data, sizeof(NodeId));
            if (draggedId && draggedId != node.id) {
                auto res = scene.SetParent(draggedId, node.id);
                if (res == Scene::SetParentResult::Cycle)
                    m_sceneFileStatus = "Cannot reparent: target is a child of the dragged node.";
                else if (res == Scene::SetParentResult::SingularParent)
                    m_sceneFileStatus = "Cannot reparent: singular parent transform.";
                else
                    m_sceneFileStatus = "Node reparented.";
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        m_selectedNode = node.id;
        ImGui::TextDisabled("Node: %s", node.name.empty() ? "Unnamed" : node.name.c_str());
        ImGui::Separator();
        if (ImGui::BeginMenu("Add Child")) {
            if (ImGui::MenuItem("Empty Child Node"))
                m_selectedNode = AddPrimitiveNode(scene, context.models, node.id, "Empty Node", 0);
            if (ImGui::MenuItem("Child Model from Disk...")) {
                m_modelDialogAction = ModelDialogAction::AddNewChildNode;
                m_modelDialogTargetNode = node.id;
                m_modelOpenFileDialog.Open("Select Model File", ".model;.gltf;.glb", m_folders.models);
            }
            if (context.models.Size() > 0 && ImGui::BeginMenu("From Loaded Model")) {
                context.models.ForEach([&](graphics::ModelHandler handle, const graphics::Model& m) {
                    String modelItemLabel = FormatModelLabel(&m, handle);
                    if (ImGui::MenuItem(modelItemLabel.c_str())) {
                        String name = m.name.empty() ? "Model" : m.name;
                        const NodeId id = scene.AddNode({ .name = name, .parent = node.id, .localTransform = math::Matrix4x4::Identity() });
                        scene.Components<ModelComponent>().Emplace(id, ModelComponent {
                            .model = handle,
                            .color = math::Vector4 { 0.8f, 0.8f, 0.8f, 1.0f },
                            .visible = true,
                        });
                        m_selectedNode = id;
                    }
                });
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("3D Primitives")) {
                if (ImGui::MenuItem("Cube"))
                    m_selectedNode = AddPrimitiveNode(scene, context.models, node.id, "Cube", 1);
                if (ImGui::MenuItem("Wall"))
                    m_selectedNode = AddPrimitiveNode(scene, context.models, node.id, "Wall", 2);
                if (ImGui::MenuItem("Ground Plane"))
                    m_selectedNode = AddPrimitiveNode(scene, context.models, node.id, "Ground Plane", 3);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Duplicate")) {
            m_selectedNode = DuplicateNode(scene, node.id);
        }
        if (node.parent) {
            if (ImGui::MenuItem("Unparent (Move to Root)")) {
                (void)scene.SetParent(node.id, NodeId { });
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Node")) {
            const size_t removedIndex = scene.IndexOf(node.id);
            scene.RemoveNode(node.id);
            m_selectedNode = scene.IdOf((std::min)(removedIndex, scene.NodeCount() - 1));
        }
        ImGui::EndPopup();
    }

    if (isOpen && hasChildren) {
        for (const size_t childIndex : nodeChildren[nodeIndex])
            DrawNodeItem(context, childIndex, nodeChildren);
        ImGui::TreePop();
    }
}

void SceneHierarchyWindow::DrawNodeDetails(ImGuiWindowContext& context, NodeId nodeId)
{
    auto& scene = context.scene;
    const Scene::Node* selected = scene.Find(nodeId);
    if (!selected) {
        ImGui::TextDisabled("No node selected");
        ImGui::TextWrapped("Select an object in the Scene Hierarchy tree to inspect and edit its properties, transforms, physics, and scripts.");
        return;
    }

    String nodeName = selected->name;
    if (InputString("Name", nodeName))
        scene.SetName(nodeId, nodeName);

    ImGui::TextDisabled("Index: %zu  |  Raw ID: %u", scene.IndexOf(nodeId), nodeId.Raw());

    const Scene::Node* currentParent = scene.Find(selected->parent);
    String parentPreview = currentParent ? NodeDisplayName(*currentParent, scene.IndexOf(currentParent->id)) : String("None (Root)");
    if (ImGui::BeginCombo("Parent", parentPreview.c_str())) {
        if (ImGui::Selectable("None (Root)", !selected->parent))
            (void)scene.SetParent(nodeId, NodeId { });

        for (const auto& candidate : scene.Nodes()) {
            if (candidate.id == nodeId)
                continue;
            String candName = NodeDisplayName(candidate, scene.IndexOf(candidate.id));
            ImGui::PushID(static_cast<int>(candidate.id.Raw()));
            if (ImGui::Selectable(candName.c_str(), selected->parent == candidate.id)) {
                auto res = scene.SetParent(nodeId, candidate.id);
                if (res == Scene::SetParentResult::Cycle)
                    m_sceneFileStatus = "Cannot reparent: cycle detected.";
                else if (res == Scene::SetParentResult::SingularParent)
                    m_sceneFileStatus = "Cannot reparent: singular parent transform.";
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate"))
        m_selectedNode = DuplicateNode(scene, nodeId);
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete Node")) {
        const size_t removedIndex = scene.IndexOf(nodeId);
        scene.RemoveNode(nodeId);
        m_selectedNode = scene.IdOf((std::min)(removedIndex, scene.NodeCount() - 1));
        return;
    }

    ImGui::Separator();

    DrawTransformSection(scene, nodeId);
    DrawModelComponent(scene, nodeId, context.models);
    DrawRigidBodyComponent(scene, nodeId);
    DrawColliderComponent(scene, nodeId, context.models);
    DrawScriptComponent(scene, nodeId);
    DrawAddComponentPopup(scene, nodeId, context.models);
}

void SceneHierarchyWindow::DrawTransformSection(Scene& scene, NodeId nodeId)
{
    const Scene::Node* selected = scene.Find(nodeId);
    if (!selected)
        return;

    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // Viewport gizmo settings
    ImGui::TextUnformatted("Gizmo:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Translate", m_gizmoOperation == GizmoOperation::Translate))
        m_gizmoOperation = GizmoOperation::Translate;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", m_gizmoOperation == GizmoOperation::Rotate))
        m_gizmoOperation = GizmoOperation::Rotate;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", m_gizmoOperation == GizmoOperation::Scale))
        m_gizmoOperation = GizmoOperation::Scale;

    ImGui::SameLine(0.0f, 16.0f);
    if (ImGui::RadioButton("World##Gizmo", !m_useLocalSpace))
        m_useLocalSpace = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Local##Gizmo", m_useLocalSpace))
        m_useLocalSpace = true;

    ImGui::Spacing();

    // Editor coordinate space
    if (ImGui::RadioButton("Local Transform", m_editLocalTransform))
        m_editLocalTransform = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("World Transform", !m_editLocalTransform))
        m_editLocalTransform = false;

    const size_t nodeIndex = scene.IndexOf(nodeId);
    const math::Matrix4x4 currentMat = m_editLocalTransform ? selected->localTransform : scene.WorldTransforms()[nodeIndex];
    const math::DecomposedTRS trs = math::DecomposeTRS(currentMat);
    const math::Vector3 euler = math::ToEuler(trs.rotation);

    float pos[3] = { trs.translation.x, trs.translation.y, trs.translation.z };
    float rot[3] = { euler.x * Rad2Deg, euler.y * Rad2Deg, euler.z * Rad2Deg };
    float scl[3] = { trs.scale.x, trs.scale.y, trs.scale.z };

    bool changed = false;
    if (ImGui::DragFloat3("Position", pos, 0.05f, 0.0f, 0.0f, "%.3f"))
        changed = true;
    if (ImGui::DragFloat3("Rotation", rot, 0.5f, 0.0f, 0.0f, "%.1f deg"))
        changed = true;
    if (ImGui::DragFloat3("Scale", scl, 0.02f, 0.001f, 1000.0f, "%.3f"))
        changed = true;

    if (changed) {
        const math::Vector3 newTrans { pos[0], pos[1], pos[2] };
        const math::Vector3 newEulerRad { rot[0] * Deg2Rad, rot[1] * Deg2Rad, rot[2] * Deg2Rad };
        const math::Quaternion newRot = math::QuaternionFromEuler(newEulerRad);
        const math::Vector3 newScale { scl[0], scl[1], scl[2] };
        const math::Matrix4x4 newMat = math::ComposeTRS(newTrans, newRot, newScale);

        if (m_editLocalTransform) {
            scene.SetLocalTransform(nodeId, newMat);
            scene.UpdateWorldTransforms();
        } else {
            if (!scene.SetWorldTransform(nodeId, newMat))
                m_sceneFileStatus = "Cannot update transform: parent transform is singular.";
        }
    }

    if (ImGui::Button("Reset Pos")) {
        pos[0] = 0.0f; pos[1] = 0.0f; pos[2] = 0.0f;
        const math::Vector3 newTrans { pos[0], pos[1], pos[2] };
        const math::Vector3 newEulerRad { rot[0] * Deg2Rad, rot[1] * Deg2Rad, rot[2] * Deg2Rad };
        const math::Matrix4x4 newMat = math::ComposeTRS(newTrans, math::QuaternionFromEuler(newEulerRad), { scl[0], scl[1], scl[2] });
        if (m_editLocalTransform) {
            scene.SetLocalTransform(nodeId, newMat);
            scene.UpdateWorldTransforms();
        } else {
            (void)scene.SetWorldTransform(nodeId, newMat);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Rot")) {
        rot[0] = 0.0f; rot[1] = 0.0f; rot[2] = 0.0f;
        const math::Matrix4x4 newMat = math::ComposeTRS({ pos[0], pos[1], pos[2] }, math::QuaternionFromEuler({ 0.0f, 0.0f, 0.0f }), { scl[0], scl[1], scl[2] });
        if (m_editLocalTransform) {
            scene.SetLocalTransform(nodeId, newMat);
            scene.UpdateWorldTransforms();
        } else {
            (void)scene.SetWorldTransform(nodeId, newMat);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Scale")) {
        scl[0] = 1.0f; scl[1] = 1.0f; scl[2] = 1.0f;
        const math::Vector3 newEulerRad { rot[0] * Deg2Rad, rot[1] * Deg2Rad, rot[2] * Deg2Rad };
        const math::Matrix4x4 newMat = math::ComposeTRS({ pos[0], pos[1], pos[2] }, math::QuaternionFromEuler(newEulerRad), { 1.0f, 1.0f, 1.0f });
        if (m_editLocalTransform) {
            scene.SetLocalTransform(nodeId, newMat);
            scene.UpdateWorldTransforms();
        } else {
            (void)scene.SetWorldTransform(nodeId, newMat);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset All")) {
        const math::Matrix4x4 newMat = math::Matrix4x4::Identity();
        if (m_editLocalTransform) {
            scene.SetLocalTransform(nodeId, newMat);
            scene.UpdateWorldTransforms();
        } else {
            (void)scene.SetWorldTransform(nodeId, newMat);
        }
    }

    if (m_editLocalTransform) {
        const math::Matrix4x4 world = scene.WorldTransforms()[nodeIndex];
        ImGui::TextDisabled("World Pos: (%.2f, %.2f, %.2f)", world(0, 3), world(1, 3), world(2, 3));
    } else {
        const math::Matrix4x4 local = selected->localTransform;
        ImGui::TextDisabled("Local Pos: (%.2f, %.2f, %.2f)", local(0, 3), local(1, 3), local(2, 3));
    }
}

void SceneHierarchyWindow::DrawModelComponent(Scene& scene, NodeId nodeId, graphics::ModelManager& models)
{
    auto* modelCompStore = scene.Components<ModelComponent>().Find(nodeId);
    if (!modelCompStore)
        return;

    bool open = ImGui::CollapsingHeader("Model Component", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 25.0f);
    if (ImGui::SmallButton("X##RemoveModel")) {
        scene.Components<ModelComponent>().Remove(nodeId);
        return;
    }

    if (!open)
        return;

    ModelComponent& comp = *modelCompStore;
    ImGui::Checkbox("Visible", &comp.visible);

    float color[4] = { comp.color.x, comp.color.y, comp.color.z, comp.color.w };
    if (ImGui::ColorEdit4("Color", color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
        comp.color = math::Vector4 { color[0], color[1], color[2], color[3] };

    const auto* model = models.Get(comp.model);
    String previewName = FormatModelLabel(model, comp.model);

    ImGui::TextUnformatted("Model Asset:");
    const float browseBtnWidth = 85.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth((std::max)(100.0f, ImGui::GetContentRegionAvail().x - browseBtnWidth - spacing));
    if (ImGui::BeginCombo("##ModelCombo", previewName.c_str())) {
        models.ForEach([&](graphics::ModelHandler handle, const graphics::Model& m) {
            String itemText = FormatModelLabel(&m, handle);
            ImGui::PushID(static_cast<int>(handle.Raw()));
            if (ImGui::Selectable(itemText.c_str(), comp.model == handle))
                comp.model = handle;
            ImGui::PopID();
        });
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...##BrowseModelDisk", ImVec2(browseBtnWidth, 0.0f))) {
        m_modelDialogAction = ModelDialogAction::AssignToComponent;
        m_modelDialogTargetNode = nodeId;
        m_modelOpenFileDialog.Open("Select Model File", ".model;.gltf;.glb", m_folders.models);
    }

    if (model) {
        ImGui::TextDisabled("Submeshes: %zu", model->submeshes.size());
        const auto bounds = model->GetLocalBounds();
        if (bounds.minBounds.x <= bounds.maxBounds.x) {
            const math::Vector3 size = bounds.maxBounds - bounds.minBounds;
            ImGui::TextDisabled("Dimensions: [%.2f, %.2f, %.2f]", size.x, size.y, size.z);
        }
    }

    ImGui::TextUnformatted("Set Primitive:");
    ImGui::SameLine();
    if (ImGui::Button("Cube")) {
        graphics::Model m;
        m.name = "Cube";
        m.AddSubmesh(GeometryPrimitives::CreateCube(1.0f));
        comp.model = models.Insert(std::move(m));
    }
    ImGui::SameLine();
    if (ImGui::Button("Wall")) {
        graphics::Model m;
        m.name = "Wall";
        m.AddSubmesh(GeometryPrimitives::CreateWall(2.0f, 2.0f, 0.2f));
        comp.model = models.Insert(std::move(m));
    }
    ImGui::SameLine();
    if (ImGui::Button("Plane")) {
        graphics::Model m;
        m.name = "Ground Plane";
        m.AddSubmesh(GeometryPrimitives::CreateGroundPlane(10.0f, 10.0f));
        comp.model = models.Insert(std::move(m));
    }

    if (model) {
        ImGui::SameLine();
        if (ImGui::Button("Open in Editor")) {
            if (const auto editor = Request<OpenModelEditor>())
                editor->open(comp.model);
        }
    }
}

void SceneHierarchyWindow::DrawRigidBodyComponent(Scene& scene, NodeId nodeId)
{
    auto* bodyStore = scene.Components<RigidBody>().Find(nodeId);
    if (!bodyStore)
        return;

    bool open = ImGui::CollapsingHeader("RigidBody (Physics)", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 25.0f);
    if (ImGui::SmallButton("X##RemoveRB")) {
        scene.Components<RigidBody>().Remove(nodeId);
        return;
    }

    if (!open)
        return;

    RigidBody& body = *bodyStore;
    const char* types[] = { "Static", "Kinematic", "Dynamic" };
    int type = static_cast<int>(body.type);
    if (ImGui::Combo("Body Type", &type, types, IM_ARRAYSIZE(types)))
        body.type = static_cast<BodyType>(type);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Static: immovable geometry\nKinematic: moves by script/animation\nDynamic: simulated physics body");

    ImGui::BeginDisabled(body.type != BodyType::Dynamic);
    ImGui::DragFloat("Mass", &body.mass, 0.05f, 0.001f, 1.0e6f, "%.3f kg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Gravity Scale", &body.gravityScale, 0.02f, -100.0f, 100.0f, "%.2f");
    ImGui::SliderFloat("Linear Damping", &body.linearDamping, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Angular Damping", &body.angularDamping, 0.0f, 1.0f, "%.3f");
    ImGui::EndDisabled();

    ImGui::DragFloat("Friction", &body.friction, 0.01f, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Restitution (Bounciness)", &body.restitution, 0.0f, 1.0f, "%.2f");

    const bool hasCollider = scene.FindComponents<Collider>() && scene.FindComponents<Collider>()->Contains(nodeId);
    if (!hasCollider) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Notice: RigidBody needs a Collider to collide.");
        if (ImGui::Button("+ Add Collider for Body")) {
            scene.Components<Collider>().Emplace(nodeId, Collider {});
        }
    }
}

void SceneHierarchyWindow::DrawColliderComponent(Scene& scene, NodeId nodeId, const graphics::ModelManager& models)
{
    auto* colStore = scene.Components<Collider>().Find(nodeId);
    if (!colStore)
        return;

    bool open = ImGui::CollapsingHeader("Collider (Physics)", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 25.0f);
    if (ImGui::SmallButton("X##RemoveCol")) {
        scene.Components<Collider>().Remove(nodeId);
        return;
    }

    if (!open)
        return;

    Collider& collider = *colStore;
    const char* shapes[] = { "Box", "Sphere", "Capsule" };
    int shape = static_cast<int>(collider.shape);
    if (ImGui::Combo("Shape", &shape, shapes, IM_ARRAYSIZE(shapes)))
        collider.shape = static_cast<ColliderShape>(shape);

    switch (collider.shape) {
    case ColliderShape::Box:
        ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, 0.02f, 0.001f, 1.0e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled("Full Size: (%.2f, %.2f, %.2f)", collider.halfExtents.x * 2.0f, collider.halfExtents.y * 2.0f, collider.halfExtents.z * 2.0f);
        break;
    case ColliderShape::Sphere:
        ImGui::DragFloat("Radius", &collider.radius, 0.02f, 0.001f, 1.0e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        break;
    case ColliderShape::Capsule:
        ImGui::DragFloat("Radius", &collider.radius, 0.02f, 0.001f, 1.0e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Half Height", &collider.halfHeight, 0.02f, 0.001f, 1.0e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        break;
    }

    ImGui::DragFloat3("Center Offset", &collider.offset.x, 0.02f, 0.0f, 0.0f, "%.3f");
    ImGui::Checkbox("Is Trigger", &collider.trigger);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Triggers report collision events on enter/exit without solid physical pushing.");

    const auto* modelComp = scene.FindComponents<ModelComponent>() ? scene.FindComponents<ModelComponent>()->Find(nodeId) : nullptr;
    const auto* model = modelComp ? models.Get(modelComp->model) : nullptr;
    if (ImGui::Button("Fit to Model")) {
        if (model) {
            Collider fitted = FitColliderToModel(model);
            fitted.shape = collider.shape;
            fitted.trigger = collider.trigger;
            collider = fitted;
        }
    }
    if (!model && ImGui::IsItemHovered())
        ImGui::SetTooltip("Node does not have an active 3D model.");

    const bool hasRigidBody = scene.FindComponents<RigidBody>() && scene.FindComponents<RigidBody>()->Contains(nodeId);
    if (!hasRigidBody) {
        ImGui::TextDisabled("Static collider (acts as immovable collision geometry)");
        if (ImGui::Button("+ Add RigidBody")) {
            scene.Components<RigidBody>().Emplace(nodeId, RigidBody {});
        }
    }
}

void SceneHierarchyWindow::DrawScriptComponent(Scene& scene, NodeId nodeId)
{
    if (!scene.Components<ScriptComponent>().Find(nodeId))
        return;
    const auto scripts = Request<ScriptInspection>();

    bool open = ImGui::CollapsingHeader("Script Component (Lua)", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 25.0f);
    if (ImGui::SmallButton("X##RemoveScript")) {
        // The instance ends first, as with entity:remove("Script")
        if (scripts && scripts->endInstance)
            scripts->endInstance(nodeId);
        scene.Components<ScriptComponent>().Remove(nodeId);
        return;
    }
    if (!open)
        return;

    DrawScriptModule(*scene.Components<ScriptComponent>().Find(nodeId));
    if (!scripts) {
        ImGui::TextDisabled("The scripts are not running: the props cannot be read");
        return;
    }
    DrawScriptInstance(nodeId, *scripts);
    ImGui::Spacing();
    DrawScriptProps(scene, nodeId, *scripts);
}

void SceneHierarchyWindow::DrawScriptModule(ScriptComponent& script)
{
    String module = script.module;
    if (InputString("Module", module))
        script.module = module;

    ImGui::SameLine();
    if (ImGui::Button("...##BrowseModules")) {
        RefreshScriptModules();
        m_scriptFilter[0] = '\0';
        ImGui::OpenPopup("PickModulePopup");
    }

    if (ImGui::BeginPopup("PickModulePopup")) {
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##ScriptFilter", "Filter scripts...", m_scriptFilter, sizeof(m_scriptFilter));
        ImGui::Separator();
        if (m_scriptModules.empty()) {
            ImGui::TextDisabled("No .lua scripts found in assets/scripts");
        }
        for (const auto& mod : m_scriptModules) {
            if (ContainsNoCase(mod, m_scriptFilter) && ImGui::MenuItem(mod.c_str()))
                script.module = mod;
        }
        ImGui::EndPopup();
    }

}

void SceneHierarchyWindow::DrawScriptInstance(NodeId nodeId, const ScriptInspection& scripts)
{
    const auto play = Request<PlayControls>();
    if (!play || !play->playing) {
        ImGui::TextDisabled("Starts on Play");
        return;
    }
    const ScriptInstanceState state = scripts.instance ? scripts.instance(nodeId) : ScriptInstanceState { };
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

void SceneHierarchyWindow::DrawScriptProps(Scene& scene, NodeId nodeId, const ScriptInspection& scripts)
{
    const String module = scene.Components<ScriptComponent>().Find(nodeId)->module;
    const auto defaults = scripts.defaults ? scripts.defaults(module) : EngineResult<ScriptProps> { };
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
    for (const auto& [name, fallback] : *defaults) {
        // Reading the defaults ran the module's code, which may change the scene: the component is looked up each time
        ScriptComponent* script = scene.Components<ScriptComponent>().Find(nodeId);
        if (!script)
            return;
        const ScriptValue* overridden = FindOverride(*script, name);
        ScriptValue value = overridden ? *overridden : fallback;
        ImGui::PushID(name.c_str());
        if (overridden)
            ImGui::PushStyleColor(ImGuiCol_Text, OverrideColor);
        const bool changed = DrawScriptValue(name.c_str(), value, scene);
        if (overridden)
            ImGui::PopStyleColor();
        if (changed) {
            SetOverride(*script, name, value);
            if (scripts.setField)
                scripts.setField(nodeId, name, value);
        }
        if (overridden) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset")) {
                RemoveOverride(*script, name);
                if (scripts.setField)
                    scripts.setField(nodeId, name, fallback);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Take the value of the script again");
        }
        ImGui::PopID();
    }
    if (ScriptComponent* script = scene.Components<ScriptComponent>().Find(nodeId))
        DrawUnknownProps(*script, *defaults);
}

void SceneHierarchyWindow::DrawUnknownProps(ScriptComponent& script, const ScriptProps& props)
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

void SceneHierarchyWindow::DrawAddComponentPopup(Scene& scene, NodeId nodeId, const graphics::ModelManager& models)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("+ Add Component", ImVec2(-1.0f, 26.0f))) {
        RefreshScriptModules();
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        const bool hasModel = scene.FindComponents<ModelComponent>() && scene.FindComponents<ModelComponent>()->Contains(nodeId);
        const bool hasRB = scene.FindComponents<RigidBody>() && scene.FindComponents<RigidBody>()->Contains(nodeId);
        const bool hasCol = scene.FindComponents<Collider>() && scene.FindComponents<Collider>()->Contains(nodeId);
        const bool hasScript = scene.FindComponents<ScriptComponent>() && scene.FindComponents<ScriptComponent>()->Contains(nodeId);

        if (hasModel) {
            ImGui::MenuItem("Model Component", nullptr, false, false);
        } else if (ImGui::BeginMenu("Model Component")) {
            if (ImGui::MenuItem("Browse from Disk (.gltf, .glb, .model)...")) {
                m_modelDialogAction = ModelDialogAction::AssignToComponent;
                m_modelDialogTargetNode = nodeId;
                m_modelOpenFileDialog.Open("Select Model File", ".model;.gltf;.glb", m_folders.models);
                graphics::Model m;
                m.name = "Model";
                m.AddSubmesh(GeometryPrimitives::CreateCube(1.0f));
                const auto handle = const_cast<graphics::ModelManager&>(models).Insert(std::move(m));
                scene.Components<ModelComponent>().Emplace(nodeId, ModelComponent {
                    .model = handle,
                    .color = math::Vector4 { 0.8f, 0.8f, 0.8f, 1.0f },
                    .visible = true,
                });
            }
            if (ImGui::MenuItem("Default Cube")) {
                graphics::Model m;
                m.name = "Cube";
                m.AddSubmesh(GeometryPrimitives::CreateCube(1.0f));
                const auto handle = const_cast<graphics::ModelManager&>(models).Insert(std::move(m));
                scene.Components<ModelComponent>().Emplace(nodeId, ModelComponent {
                    .model = handle,
                    .color = math::Vector4 { 0.8f, 0.8f, 0.8f, 1.0f },
                    .visible = true,
                });
            }
            if (ImGui::MenuItem("Default Wall")) {
                graphics::Model m;
                m.name = "Wall";
                m.AddSubmesh(GeometryPrimitives::CreateWall(2.0f, 2.0f, 0.2f));
                const auto handle = const_cast<graphics::ModelManager&>(models).Insert(std::move(m));
                scene.Components<ModelComponent>().Emplace(nodeId, ModelComponent {
                    .model = handle,
                    .color = math::Vector4 { 0.8f, 0.8f, 0.8f, 1.0f },
                    .visible = true,
                });
            }
            if (ImGui::MenuItem("Default Ground Plane")) {
                graphics::Model m;
                m.name = "Ground Plane";
                m.AddSubmesh(GeometryPrimitives::CreateGroundPlane(10.0f, 10.0f));
                const auto handle = const_cast<graphics::ModelManager&>(models).Insert(std::move(m));
                scene.Components<ModelComponent>().Emplace(nodeId, ModelComponent {
                    .model = handle,
                    .color = math::Vector4 { 0.8f, 0.8f, 0.8f, 1.0f },
                    .visible = true,
                });
            }
            if (models.Size() > 0) {
                ImGui::Separator();
                ImGui::TextDisabled("Loaded Models:");
                models.ForEach([&](graphics::ModelHandler handle, const graphics::Model& m) {
                    String label = FormatModelLabel(&m, handle);
                    if (ImGui::MenuItem(label.c_str())) {
                        scene.Components<ModelComponent>().Emplace(nodeId, ModelComponent {
                            .model = handle,
                            .color = math::Vector4 { 0.8f, 0.8f, 0.8f, 1.0f },
                            .visible = true,
                        });
                    }
                });
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("RigidBody (Physics)", nullptr, false, !hasRB)) {
            scene.Components<RigidBody>().Emplace(nodeId, RigidBody {
                .type = BodyType::Dynamic,
                .mass = 1.0f,
                .friction = 0.5f,
                .restitution = 0.0f,
                .linearDamping = 0.05f,
                .angularDamping = 0.05f,
                .gravityScale = 1.0f,
            });
        }

        if (ImGui::MenuItem("Collider (Physics)", nullptr, false, !hasCol)) {
            const auto* modelComp = scene.FindComponents<ModelComponent>() ? scene.FindComponents<ModelComponent>()->Find(nodeId) : nullptr;
            const auto* model = modelComp ? models.Get(modelComp->model) : nullptr;
            Collider col = FitColliderToModel(model);
            scene.Components<Collider>().Emplace(nodeId, std::move(col));
        }

        if (hasScript) {
            ImGui::MenuItem("Script Component (Lua)", nullptr, false, false);
        } else if (ImGui::BeginMenu("Script Component (Lua)")) {
            if (ImGui::MenuItem("Empty Script Component")) {
                scene.Components<ScriptComponent>().Emplace(nodeId, ScriptComponent {
                    .module = "",
                    .props = {},
                });
            }
            ImGui::Separator();
            for (const auto& mod : m_scriptModules) {
                if (ImGui::MenuItem(mod.c_str())) {
                    scene.Components<ScriptComponent>().Emplace(nodeId, ScriptComponent {
                        .module = mod,
                        .props = {},
                    });
                }
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }
}

NodeId SceneHierarchyWindow::DuplicateNode(Scene& scene, NodeId sourceId)
{
    const Scene::Node* source = scene.Find(sourceId);
    if (!source)
        return { };

    Scene::NodeDesc copy {
        .name = source->name + " Copy",
        .parent = source->parent,
        .localTransform = source->localTransform,
    };
    copy.localTransform(0, 3) += 1.0f;
    const NodeId newId = scene.AddNode(std::move(copy));
    scene.CopyComponents(sourceId, newId);
    m_sceneFileStatus = "Duplicated node.";
    return newId;
}

NodeId SceneHierarchyWindow::AddPrimitiveNode(Scene& scene, graphics::ModelManager& models, NodeId parentId, const char* name, int primitiveType)
{
    if (primitiveType == 0) {
        return scene.AddNode(Scene::NodeDesc {
            .name = name,
            .parent = parentId,
            .localTransform = math::Matrix4x4::Identity(),
        });
    }

    graphics::Model model;
    model.name = name;
    math::Vector4 color { 0.8f, 0.8f, 0.8f, 1.0f };
    math::Vector3 pos { 0.0f, 0.0f, 0.0f };

    if (primitiveType == 1) { // Cube
        model.AddSubmesh(GeometryPrimitives::CreateCube(1.0f));
        color = math::Vector4 { 0.7f, 0.75f, 0.8f, 1.0f };
        pos = math::Vector3 { 0.0f, 0.5f, 0.0f };
    } else if (primitiveType == 2) { // Wall
        model.AddSubmesh(GeometryPrimitives::CreateWall(2.0f, 2.0f, 0.2f));
        color = math::Vector4 { 0.6f, 0.6f, 0.65f, 1.0f };
        pos = math::Vector3 { 0.0f, 1.0f, 0.0f };
    } else if (primitiveType == 3) { // Ground Plane
        model.AddSubmesh(GeometryPrimitives::CreateGroundPlane(10.0f, 10.0f));
        color = math::Vector4 { 0.4f, 0.5f, 0.4f, 1.0f };
        pos = math::Vector3 { 0.0f, 0.0f, 0.0f };
    }

    const auto handle = models.Insert(std::move(model));
    const NodeId id = scene.AddNode(Scene::NodeDesc {
        .name = name,
        .parent = parentId,
        .localTransform = math::Matrix4x4::Translation(pos),
    });

    scene.Components<ModelComponent>().Emplace(id, ModelComponent {
        .model = handle,
        .color = color,
        .visible = true,
    });

    return id;
}

void SceneHierarchyWindow::LoadSceneFile(ImGuiWindowContext& context, const Path& file)
{
    auto result = resmgr::Serializer::LoadScene(file, m_folders.assets);
    if (!result) {
        m_sceneFileStatus = result.error().message;
        return;
    }
    context.scene.Clear();
    context.models.Clear();
    const auto handlers = context.models.InsertAll(std::move(result->models), result->modelFiles);
    (void)context.scene.Append(std::move(*result), handlers);
    m_selectedNode = context.scene.IdOf(0);
    m_sceneFileStatus = "Scene loaded from " + String(file.string().c_str()) + ".";
    ShowSceneFile(file);
}

void SceneHierarchyWindow::ShowSceneFile(const Path& file)
{
    const String relative(file.LexicallyNormal().LexicallyRelative(m_folders.scenes.LexicallyNormal()).generic_string().c_str());
    const String shown = relative.empty() || relative.starts_with("..") ? String(file.string().c_str()) : relative;
    std::snprintf(m_sceneFilePath, sizeof(m_sceneFilePath), "%s", shown.c_str());
}

void SceneHierarchyWindow::AcceptDroppedModel(ImGuiWindowContext& context)
{
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ModelFilePayload)) {
        const Path file(StringView(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize)));
        // A model of the project stays its file, anything else is imported
        auto imported = resmgr::AssetImport::ImportModel(file, m_folders.assets);
        if (imported) {
            const String name = imported->model.name;
            const auto model = context.models.Insert(std::move(imported->model), std::move(imported->file));
            m_selectedNode = context.scene.AddNode({ .name = name });
            context.scene.Components<ModelComponent>().Emplace(m_selectedNode, ModelComponent { .model = model });
            m_sceneFileStatus = "Added node \"" + name + "\".";
        } else {
            m_sceneFileStatus = "Failed to add the model: " + imported.error().message;
        }
    }
    ImGui::EndDragDropTarget();
}

String SceneHierarchyWindow::UnusedSceneFileName() const
{
    String fileName = "new.scene";
    for (size_t suffix = 1; FileSystem::Exists(m_folders.scenes / fileName); ++suffix)
        fileName = "new_" + String(std::to_string(suffix).c_str()) + ".scene";
    return fileName;
}

EngineResult<void> SceneHierarchyWindow::SaveScene(ImGuiWindowContext& context, const Path& path)
{
    if (auto newModels = resmgr::Serializer::SaveNewModels(context.scene, context.models, m_folders.assets); !newModels)
        return MakeUnexpected(newModels.error());
    return resmgr::Serializer::SaveScene(context.scene, context.models, m_folders.assets, path);
}

void SceneHierarchyWindow::RefreshScriptModules()
{
    m_scriptModules.clear();
    const Path root = m_folders.scripts;
    Vector<Path> folders { root };
    while (!folders.empty()) {
        const Path folder = std::move(folders.back());
        folders.pop_back();
        std::error_code error;
        for (auto it = Directory(folder).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
            const Path path = it.CurrentPath();
            std::error_code typeError;
            if (it.IsDirectory(typeError)) {
                if (!path.Filename().string().starts_with("."))
                    folders.push_back(path);
            } else {
                const String rel(path.LexicallyRelative(root).generic_string().c_str());
                if (rel.ends_with(".lua") && !rel.starts_with("..")) {
                    String mod = rel.substr(0, rel.size() - 4);
                    if (mod == "init")
                        continue;
                    if (mod.ends_with("/init"))
                        mod.resize(mod.size() - 5);
                    for (char& c : mod) {
                        if (c == '/')
                            c = '.';
                    }
                    if (!mod.empty())
                        m_scriptModules.push_back(std::move(mod));
                }
            }
        }
    }
    std::sort(m_scriptModules.begin(), m_scriptModules.end());
    m_scriptModulesLoaded = true;
}

} // namespace elm
