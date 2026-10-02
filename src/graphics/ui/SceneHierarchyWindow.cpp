#include "graphics/ui/SceneHierarchyWindow.hpp"

#include "graphics/MeshDataStorage.hpp"
#include "graphics/render/MeshManager.h"
#include "imgui.h"
#include "math/Primitivs.hpp"

#include <algorithm>
#include <cstdio>

namespace elm {

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

void SceneHierarchyWindow::Render(RenderSystem&, Scene& scene, const Camera&, const FrameStats&)
{
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
            TestScenes::BuildScene(static_cast<ScenePreset>(preset), static_cast<uint32_t>(1500), scene);
            m_selectedInstance = 0;
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
            instance = scene.instances[m_selectedInstance];
            instance.renderMesh = render::createMesh(getMeshData(instance.meshData));
            instance.worldTransform(0, 3) += 1.0f;
        } else {
            const auto meshData = GeometryPrimitives::CreateCube(1.0f);
            instance.meshData = storeMeshData(meshData);
            instance.renderMesh = render::createMesh(meshData);
            instance.localBounds = meshData.localBounds;
            instance.worldTransform = Matrix4x4::Translation(Vector3 { 0.0f, 0.5f, 0.0f });
        }
        instance.visible = true;
        scene.instances.push_back(instance);
        m_selectedInstance = scene.instances.size() - 1;
    }
    ImGui::SameLine();
    if (!scene.instances.empty() && ImGui::Button("Remove Selected")) {
        render::destroyMesh(scene.instances[m_selectedInstance].renderMesh);
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
        char label[48];
        std::snprintf(label, sizeof(label), "Instance %zu", index);
        if (ImGui::Selectable(label, m_selectedInstance == index)) {
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