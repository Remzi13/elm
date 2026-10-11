#include "graphics/ui/SceneHierarchyWindow.hpp"

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
        int preset = static_cast<int>(scene.preset);
        if (ImGui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets))) {
            TestScenes::BuildScene(static_cast<ScenePreset>(preset), 1500, scene, context.models,
                context.renderSystem.Resources());
            m_selectedInstance = 0;
        }
    }
    ImGui::InputText("Scene file", m_sceneFilePath, sizeof(m_sceneFilePath));
    ImGui::SameLine();
    if (ImGui::Button("Save Scene")) {
        auto result = resmgr::Serializer::SaveScene(scene, context.models, m_sceneFilePath);
        m_sceneFileStatus = result ? "Scene saved." : result.error().message;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Scene")) {
        auto result = resmgr::Serializer::LoadScene(scene, context.models,
            context.renderSystem.Resources(), m_sceneFilePath);
        if (result) {
            m_selectedInstance = 0;
            m_sceneFileStatus = "Scene loaded.";
        } else {
            m_sceneFileStatus = result.error().message;
        }
    }
    if (!m_sceneFileStatus.empty())
        ImGui::TextWrapped("%s", m_sceneFileStatus.c_str());

    if (ImGui::CollapsingHeader("glTF", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Load glTF Model")) {
            m_gltfDialogAction = GltfDialogAction::LoadModel;
            m_gltfFileDialog.Open("Select glTF Model", ".gltf");
        }
        ImGui::SameLine();
        if (ImGui::Button("Load glTF Scene")) {
            m_gltfDialogAction = GltfDialogAction::LoadScene;
            m_gltfFileDialog.Open("Select glTF Scene", ".gltf");
        }
        if (ImGui::Button("Export Scene to glTF")) {
            m_gltfDialogAction = GltfDialogAction::ExportScene;
            m_gltfFileDialog.SaveAs("Export glTF Scene", ".gltf", "scene.gltf");
        }
        if (!scene.nodes.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Export Selected Model")) {
                m_gltfDialogAction = GltfDialogAction::ExportModel;
                m_gltfFileDialog.SaveAs("Export glTF Model", ".gltf", "model.gltf");
            }
        }
    }
    if (const auto selectedPath = m_gltfFileDialog.Draw()) {
        if (m_gltfDialogAction == GltfDialogAction::ExportScene) {
            auto result = resmgr::GltfExporter::Export(*selectedPath, scene, context.models);
            m_sceneFileStatus = result ? "Scene exported to glTF." : result.error().message;
        } else if (m_gltfDialogAction == GltfDialogAction::LoadScene) {
            auto result = resmgr::GltfExporter::LoadScene(scene, context.models,
                context.renderSystem.Resources(), *selectedPath);
            if (result) {
                m_selectedInstance = 0;
                m_sceneFileStatus = "glTF scene loaded.";
            } else {
                m_sceneFileStatus = result.error().message;
            }
        } else if (m_gltfDialogAction == GltfDialogAction::LoadModel) {
            Scene imported;
            resmgr::ModelManager importedModels;
            const size_t firstImportedNode = scene.nodes.size();
            auto result = resmgr::GltfExporter::LoadScene(imported, importedModels,
                context.renderSystem.Resources(), *selectedPath);
            if (!result) {
                m_sceneFileStatus = result.error().message;
            } else {
                const auto remapping = importedModels.MoveTo(context.models);
                for (auto& node : imported.nodes) {
                    if (const auto model = remapping.find(node.model); model != remapping.end())
                        node.model = model->second;
                }
                scene.Append(std::move(imported));
                if (scene.nodes.size() > firstImportedNode)
                    m_selectedInstance = firstImportedNode;
                m_sceneFileStatus = "glTF model loaded.";
            }
        } else if (m_selectedInstance < scene.nodes.size()) {
            const auto& node = scene.nodes[m_selectedInstance];
            const auto* model = context.models.Get(node.model);
            if (!model) {
                m_sceneFileStatus = "Selected node has no model.";
            } else {
                auto result = resmgr::GltfExporter::ExportModel(*model, *selectedPath);
                m_sceneFileStatus = result ? "Model exported to glTF." : result.error().message;
            }
        }
    }
    ImGui::Separator();

    if (scene.nodes.empty()) {
        m_selectedInstance = 0;
    } else {
        m_selectedInstance = (std::min)(m_selectedInstance, scene.nodes.size() - 1);
    }

    if (ImGui::Button("Add Object")) {
        if (!scene.nodes.empty()) {
            auto node = scene.nodes[m_selectedInstance];
            node.worldTransform(0, 3) += 1.0f;
            node.localTransform(0, 3) += 1.0f;
            node.name += " Copy";
            scene.nodes.push_back(std::move(node));
        } else {
            const auto modelHandler = context.models.Create("Cube");
            auto* model = context.models.Get(modelHandler);
            model->AddSubmesh(GeometryPrimitives::CreateCube(1.0f));
            model->Upload(context.renderSystem.Resources());
            auto& node = scene.AddNode(modelHandler, "Cube");
            node.localTransform = Matrix4x4::Translation(Vector3 { 0.0f, 0.5f, 0.0f });
            node.worldTransform = node.localTransform;
            node.color = Vector4 { 0.8f, 0.8f, 0.8f, 1.0f };
        }
        m_selectedInstance = scene.nodes.size() - 1;
    }
    ImGui::SameLine();
    if (!scene.nodes.empty() && ImGui::Button("Remove Selected")) {
        const size_t removedIndex = m_selectedInstance;
        const size_t parentIndex = scene.nodes[removedIndex].parentIndex;
        for (auto& node : scene.nodes) {
            if (node.parentIndex == removedIndex)
                node.parentIndex = parentIndex;
            else if (node.parentIndex != Scene::NoNode && node.parentIndex > removedIndex)
                --node.parentIndex;
        }
        scene.nodes.erase(scene.nodes.begin() + removedIndex);
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
