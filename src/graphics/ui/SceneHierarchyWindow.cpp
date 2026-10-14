#include "graphics/ui/SceneHierarchyWindow.hpp"

#include "core/AssetPaths.hpp"
#include "render/RenderSystem.hpp"
#include "resmgr/GltfExporter.hpp"
#include "resmgr/Serializer.hpp"

#include "imgui.h"
#include "math/Primitivs.hpp"

#include <algorithm>
#include <cstdio>

namespace elm {
namespace {

int ResizeInputBuffer(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
        return 0;

    auto& buffer = *static_cast<Vector<char>*>(data->UserData);
    buffer.resize(static_cast<size_t>(data->BufTextLen) + 1);
    data->Buf = buffer.data();
    return 0;
}

} // namespace

void SceneHierarchyWindow::OnAttach()
{
    m_querySubscriptions.push_back(RegisterQuery<SelectedSceneInstance>([this] {
        return SelectedSceneInstance { m_selectedInstance };
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
    auto& scene = context.scene;
    if (!m_visible)
        return;

    ImGui::SetNextWindowSize(ImVec2(640.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(GetName().data(), &m_visible)) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Scene Preset", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* presets[] = { "Box", "The Great Wall & City Grid", "Rooms & Corridors", "Physics Barrier Sandbox" };
        static int preset = 0;
        if (ImGui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets))) {
            TestScenes::BuildScene(static_cast<ScenePreset>(preset), 1500, scene, context.models);
            m_selectedInstance = 0;
        }
    }
    ImGui::InputText("Scene file", m_sceneFilePath, sizeof(m_sceneFilePath));
    ImGui::SameLine();
    // A relative scene file name is taken from the scenes asset folder
    const Path scenePath = Path(m_sceneFilePath).IsAbsolute() ? Path(m_sceneFilePath) : AssetPaths::Scenes() / m_sceneFilePath;
    if (ImGui::Button("Save Scene")) {
        // In Play mode the scene is the running game: Stop brings the edited one back
        if (const auto play = Request<PlayControls>(); play && play->playing) {
            m_sceneFileStatus = "Stop first: in Play mode the scene is the running game.";
        } else {
            auto result = resmgr::Serializer::SaveScene(scene, context.models, scenePath, AssetPaths::Models());
            m_sceneFileStatus = result ? "Scene saved to " + String(scenePath.string().c_str()) + "." : result.error().message;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Scene")) {
        auto result = resmgr::Serializer::LoadScene(scenePath);
        if (result) {
            scene.Clear();
            context.models.Clear();
            scene.Append(std::move(*result), context.models);
            m_selectedInstance = 0;
            m_sceneFileStatus = "Scene loaded.";
        } else {
            m_sceneFileStatus = result.error().message;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Export glTF..."))
        m_gltfFileDialog.SaveAs("Export Scene to glTF", ".gltf", "scene.gltf", AssetPaths::Scenes());
    if (const auto path = m_gltfFileDialog.Draw()) {
        auto result = resmgr::GltfExporter::Export(*path, scene, context.models);
        m_sceneFileStatus = result ? "Scene exported to " + String(path->string().c_str()) + "." : result.error().message;
    }
    if (!m_sceneFileStatus.empty())
        ImGui::TextWrapped("%s", m_sceneFileStatus.c_str());

    if (ImGui::Button("Model Editor...")) {
        if (const auto editor = Request<OpenModelEditor>())
            editor->open({});
    }
    if (scene.nodes.empty()) {
        m_selectedInstance = 0;
    } else {
        m_selectedInstance = (std::min)(m_selectedInstance, scene.nodes.size() - 1);
    }

    if (ImGui::Button("Add Object")) {
        if (!scene.nodes.empty()) {
            const NodeId source = scene.IdOf(m_selectedInstance);
            auto node = scene.nodes[m_selectedInstance];
            node.worldTransform(0, 3) += 1.0f;
            node.localTransform(0, 3) += 1.0f;
            node.name += " Copy";
            const NodeId copy = scene.AppendNode(std::move(node)).id;
            scene.CopyComponents(source, copy);
        } else {            
            graphics::Model model;
            model.name = "Cube";            
            model.AddSubmesh(GeometryPrimitives::CreateCube(1.0f));
            auto& node = scene.AddNode(context.models.Insert(std::move(model)), "Cube");
            node.localTransform = Matrix4x4::Translation(Vector3 { 0.0f, 0.5f, 0.0f });
            node.worldTransform = node.localTransform;
            node.color = Vector4 { 0.8f, 0.8f, 0.8f, 1.0f };
        }
        m_selectedInstance = scene.nodes.size() - 1;
    }
    ImGui::SameLine();
    if (!scene.nodes.empty() && ImGui::Button("Remove Selected")) {
        const size_t removedIndex = m_selectedInstance;
        scene.RemoveNode(removedIndex);
        m_selectedInstance = scene.nodes.empty() ? 0 : (std::min)(removedIndex, scene.nodes.size() - 1);
    }
    ImGui::Separator();

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float listWidth = (std::min)(220.0f, availableWidth * 0.4f);
    ImGui::BeginChild("SceneObjects", ImVec2(listWidth, 0.0f), true);
    Vector<Vector<size_t>> nodeChildren(scene.nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex) {
        const size_t parentIndex = scene.nodes[nodeIndex].parentIndex;
        if (parentIndex < scene.nodes.size())
            nodeChildren[parentIndex].push_back(nodeIndex);
    }

    auto drawNode = [&](auto&& self, size_t nodeIndex) -> void {
        const auto& node = scene.nodes[nodeIndex];
        String label = node.name.empty() ? "Node " + String(std::to_string(nodeIndex)) : node.name;
        label += "##node";
        label += std::to_string(nodeIndex);
        const bool hasChildren = !nodeChildren[nodeIndex].empty();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        const bool isOpen = ImGui::TreeNodeEx(label.c_str(), flags);
        if (ImGui::IsItemClicked())
            m_selectedInstance = nodeIndex;
        if (!isOpen || !hasChildren)
            return;
        for (const size_t childIndex : nodeChildren[nodeIndex])
            self(self, childIndex);
        ImGui::TreePop();
    };
    for (size_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex) {
        if (scene.nodes[nodeIndex].parentIndex == Scene::NoNode)
            drawNode(drawNode, nodeIndex);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("NodeDetails", ImVec2(0.0f, 0.0f), true);
    if (scene.nodes.empty()) {
        ImGui::TextDisabled("No scene nodes");
        ImGui::TextDisabled("Select an object to manipulate it.");
    } else {
        if (ImGui::RadioButton("Translate", m_gizmoOperation == GizmoOperation::Translate))
            m_gizmoOperation = GizmoOperation::Translate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", m_gizmoOperation == GizmoOperation::Rotate))
            m_gizmoOperation = GizmoOperation::Rotate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", m_gizmoOperation == GizmoOperation::Scale))
            m_gizmoOperation = GizmoOperation::Scale;
        ImGui::SameLine();
        if (ImGui::RadioButton("World", !m_useLocalSpace))
            m_useLocalSpace = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("Local", m_useLocalSpace))
            m_useLocalSpace = true;
        ImGui::Separator();

        auto& node = scene.nodes[m_selectedInstance];
        ImGui::Text("Node %zu", m_selectedInstance);
        ImGui::Separator();
        Vector<char> nameBuffer(node.name.begin(), node.name.end());
        nameBuffer.push_back('\0');
        if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size(),
                ImGuiInputTextFlags_CallbackResize, ResizeInputBuffer, &nameBuffer)) {
            node.name.assign(nameBuffer.data(), std::char_traits<char>::length(nameBuffer.data()));
        }
        ImGui::Checkbox("Visible", &node.visible);
        float color[4] = { node.color.x, node.color.y, node.color.z, node.color.w };
        if (ImGui::ColorEdit4("Color", color))
            node.color = Vector4 { color[0], color[1], color[2], color[3] };

        if (const auto* model = context.models.Get(node.model)) {
            ImGui::Text("Model: %s (%zu submeshes)", model->name.empty() ? "Unnamed" : model->name.c_str(),
                model->submeshes.size());
            ImGui::SameLine();
            if (ImGui::Button("Edit Model")) {
                if (const auto editor = Request<OpenModelEditor>())
                    editor->open(node.model);
            }
        } else {
            ImGui::TextDisabled("No model");
        }

        float position[3] = {
            node.worldTransform(0, 3),
            node.worldTransform(1, 3),
            node.worldTransform(2, 3)
        };
        if (ImGui::DragFloat3("World Position", position, 0.1f, 0.0f, 0.0f, "%.3f")) {
            auto worldTransform = node.worldTransform;
            worldTransform(0, 3) = position[0];
            worldTransform(1, 3) = position[1];
            worldTransform(2, 3) = position[2];
            if (!scene.SetWorldTransform(m_selectedInstance, worldTransform))
                m_sceneFileStatus = "Cannot update the node because its parent transform is singular.";
        }
        if (ImGui::Button("Reset Position")) {
            auto worldTransform = node.worldTransform;
            worldTransform(0, 3) = 0.0f;
            worldTransform(1, 3) = 0.0f;
            worldTransform(2, 3) = 0.0f;
            if (!scene.SetWorldTransform(m_selectedInstance, worldTransform))
                m_sceneFileStatus = "Cannot update the node because its parent transform is singular.";
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace elm
