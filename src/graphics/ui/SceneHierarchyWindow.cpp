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
            TestScenes::BuildScene(static_cast<ScenePreset>(preset), static_cast<uint32_t>(1500), scene,
                context.renderSystem.Resources());
            m_selectedInstance = 0;
        }
    }
    ImGui::InputText("Scene file", m_sceneFilePath, sizeof(m_sceneFilePath));
    ImGui::SameLine();
    if (ImGui::Button("Save Scene")) {
        auto result = resmgr::Serializer::SaveScene(scene, m_sceneFilePath);
        if (result) {
            m_sceneFileStatus = "Scene saved.";
        } else {
            m_sceneFileStatus = result.error().message;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Scene")) {
        auto result = resmgr::Serializer::LoadScene(scene, context.renderSystem.Resources(), m_sceneFilePath);
        if (result) {
            m_selectedInstance = 0;
            m_sceneFileStatus = "Scene loaded.";
        } else {
            m_sceneFileStatus = result.error().message;
        }
    }
    if (!m_sceneFileStatus.empty()) {
        ImGui::TextWrapped("%s", m_sceneFileStatus.c_str());
    }
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
        if (!scene.instances.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Export Selected Model")) {
                m_gltfDialogAction = GltfDialogAction::ExportModel;
                m_gltfFileDialog.SaveAs("Export glTF Model", ".gltf", "model.gltf");
            }
        }
    }
    if (const auto selectedPath = m_gltfFileDialog.Draw()) {
        if (m_gltfDialogAction == GltfDialogAction::ExportScene) {
            auto result = resmgr::GltfExporter::Export(*selectedPath, scene);
            m_sceneFileStatus = result ? "Scene exported to glTF." : result.error().message;
        } else if (m_gltfDialogAction == GltfDialogAction::LoadScene) {
            auto result = resmgr::GltfExporter::LoadScene(scene, context.renderSystem.Resources(), *selectedPath);
            if (!result) {
                m_sceneFileStatus = result.error().message;
            } else {
                m_selectedInstance = 0;
                m_sceneFileStatus = "glTF scene loaded.";
            }
        } else if (m_gltfDialogAction == GltfDialogAction::LoadModel) {
            auto model = resmgr::GltfExporter::LoadModel(*selectedPath);
            if (!model) {
                m_sceneFileStatus = model.error().message;
            } else {
                auto instance = scene.MakeInstance(context.renderSystem.Resources(), model->meshData);
                instance.color = model->color;
                instance.name = model->name;
                scene.instances.push_back(std::move(instance));
                m_selectedInstance = scene.instances.size() - 1;
                m_sceneFileStatus = "glTF model loaded.";
            }
        } else if (!scene.instances.empty()) {
            const auto selectedIndex = (std::min)(m_selectedInstance, scene.instances.size() - 1);
            const auto& instance = scene.instances[selectedIndex];
            if (!instance.meshData) {
                m_sceneFileStatus = "Selected instance has no mesh data.";
            } else {
                resmgr::Model model{ *instance.meshData, instance.color, instance.name };
                model.color = instance.color;
                model.name = instance.name;
                auto result = resmgr::GltfExporter::ExportModel(model, *selectedPath);
                m_sceneFileStatus = result ? "Model exported to glTF." : result.error().message;
            }
        }
    }
    ImGui::Separator();

    if (scene.instances.empty()) {
        m_selectedInstance = 0;
    } else {
        m_selectedInstance = (std::min)(m_selectedInstance, scene.instances.size() - 1);
    }

    if (ImGui::Button("Add Object")) {
        Scene::Instance instance;
        if (!scene.instances.empty()) {
            // Copies share the source mesh and are drawn in the same instanced batch
            instance = scene.instances[m_selectedInstance];
            instance.worldTransform(0, 3) += 1.0f;
        } else {
            instance = scene.MakeInstance(context.renderSystem.Resources(), GeometryPrimitives::CreateCube(1.0f));
            instance.worldTransform = Matrix4x4::Translation(Vector3 { 0.0f, 0.5f, 0.0f });
        }
        instance.visible = true;
        scene.instances.push_back(instance);
        m_selectedInstance = scene.instances.size() - 1;
    }
    ImGui::SameLine();
    if (!scene.instances.empty() && ImGui::Button("Remove Selected")) {
        // The mesh stays owned by the scene, other instances may use it
        scene.instances.erase(scene.instances.begin() + m_selectedInstance);
        if (scene.instances.empty()) {
            m_selectedInstance = 0;
        } else {
            m_selectedInstance = (std::min)(m_selectedInstance, scene.instances.size() - 1);
        }
    }
    ImGui::Separator();

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float listWidth = (std::min)(220.0f, availableWidth * 0.4f);
    ImGui::BeginChild("SceneObjects", ImVec2(listWidth, 0.0f), true);
    for (size_t index = 0; index < scene.instances.size(); ++index) {
        String label = scene.instances[index].name;
        if (label.empty()) {
            label = "Instance ";
            label += std::to_string(index);
        }
        label += "##";
        label += std::to_string(index);
        if (ImGui::Selectable(label.c_str(), m_selectedInstance == index)) {
            m_selectedInstance = index;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("InstanceDetails", ImVec2(0.0f, 0.0f), true);
    if (scene.instances.empty()) {
        ImGui::TextDisabled("No scene instances");
        ImGui::TextDisabled("Select an object to manipulate it.");
    } else {
        if (ImGui::RadioButton("Translate", m_gizmoOperation == GizmoOperation::Translate)) {
            m_gizmoOperation = GizmoOperation::Translate;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", m_gizmoOperation == GizmoOperation::Rotate)) {
            m_gizmoOperation = GizmoOperation::Rotate;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", m_gizmoOperation == GizmoOperation::Scale)) {
            m_gizmoOperation = GizmoOperation::Scale;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("World", !m_useLocalSpace)) {
            m_useLocalSpace = false;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Local", m_useLocalSpace)) {
            m_useLocalSpace = true;
        }
        ImGui::Separator();

        auto& instance = scene.instances[m_selectedInstance];
        ImGui::Text("Instance %zu", m_selectedInstance);
        ImGui::Separator();
        Vector<char> nameBuffer(instance.name.begin(), instance.name.end());
        nameBuffer.push_back('\0');
        if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size(),
                ImGuiInputTextFlags_CallbackResize, ResizeInputBuffer, &nameBuffer)) {
            instance.name.assign(nameBuffer.data(), std::char_traits<char>::length(nameBuffer.data()));
        }
        ImGui::Checkbox("Visible", &instance.visible);
        float color[4] = { instance.color.x, instance.color.y, instance.color.z, instance.color.w };
        if (ImGui::ColorEdit4("Color", color)) {
            instance.color = Vector4 { color[0], color[1], color[2], color[3] };
        }

        float position[3] = {
            instance.worldTransform(0, 3),
            instance.worldTransform(1, 3),
            instance.worldTransform(2, 3)
        };
        if (ImGui::DragFloat3("World Position", position, 0.1f, 0.0f, 0.0f, "%.3f")) {
            instance.worldTransform(0, 3) = position[0];
            instance.worldTransform(1, 3) = position[1];
            instance.worldTransform(2, 3) = position[2];
        }
        if (ImGui::Button("Reset Position")) {
            instance.worldTransform(0, 3) = 0.0f;
            instance.worldTransform(1, 3) = 0.0f;
            instance.worldTransform(2, 3) = 0.0f;
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace elm