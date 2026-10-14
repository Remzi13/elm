#include "graphics/ui/EngineViewportWindow.hpp"

#include "core/Log.hpp"
#include "graphics/ui/WindowMessages.hpp"
#include "render/ViewPort.hpp"

#include "imgui.h"
#include "ImGuizmo.h"

#include <array>

namespace elm {

	namespace {

		ImTextureID ToTextureId(render::TextureHandle texture)
        {
			if (!texture.IsValid())
				return nullptr;
			// Real texture views are aligned pointers, so the lowest bit marks an engine handle
            const auto value = (static_cast<uintptr_t>(texture.Raw()) << 1) | 1;
            return reinterpret_cast<ImTextureID>(value);
		}

		std::array<float, 16> ToGizmoMatrix(const Matrix4x4& matrix) {
			std::array<float, 16> result{};
			for (size_t row = 0; row < 4; ++row) {
				for (size_t column = 0; column < 4; ++column) {
					result[column * 4 + row] = matrix(row, column);
				}
			}
			return result;
		}

		Matrix4x4 FromGizmoMatrix(const std::array<float, 16>& matrix) {
			Matrix4x4 result;
			for (size_t row = 0; row < 4; ++row) {
				for (size_t column = 0; column < 4; ++column) {
					result(row, column) = matrix[column * 4 + row];
				}
			}
			return result;
		}

	}

	void EngineViewportWindow::Render(ImGuiWindowContext& context) {
		auto& scene = context.scene;
		auto& camera = context.camera;
		auto& viewPort = context.viewPort;
		m_hovered = false;
		m_focused = false;
		if (!m_visible) return;

		ImGui::SetNextWindowPos(ImVec2(460.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}
		m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		// Edit / Play mode, provided by the Inspector of the scripts; F5 switches too
		if (const auto play = Request<PlayControls>()) {
			if (ImGui::Button(play->playing ? "Stop" : "Play")
				|| (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F5, false)))
				play->toggle();
			ImGui::SameLine();
			if (play->playing)
				ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.25f, 1.0f), "Playing: changes are undone on Stop (F5)");
			else
				ImGui::TextDisabled("Edit mode (F5 plays)");
		}

		const auto selectedInstance = Request<SelectedSceneInstance>();
		const auto gizmoSettings = Request<GizmoSettings>();
		const bool hasSelection = selectedInstance && selectedInstance->index < scene.nodes.size();

		if (auto texture = ToTextureId(viewPort.GetColorTexture())) {
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const auto* viewport = ImGui::GetWindowViewport();
			const float dpiScale = viewport ? viewport->DpiScale : 1.0f;
			if (available.x > 0.0f && available.y > 0.0f) {
				const ImVec2 imagePosition = ImGui::GetCursorScreenPos();
				viewPort.SetSize({
					static_cast<uint32_t>(available.x * dpiScale),
					static_cast<uint32_t>(available.y * dpiScale)
				});
				camera.SetAspect(available.x / available.y);
				ImGui::Image(texture, available);
				// Popups and other windows on top of the image block the hover
				m_hovered = ImGui::IsItemHovered();

				if (hasSelection && gizmoSettings && scene.nodes[selectedInstance->index].visible) {
					auto& instance = scene.nodes[selectedInstance->index];
					ImGuizmo::BeginFrame();
					ImGuizmo::SetDrawlist();
					ImGuizmo::SetRect(imagePosition.x, imagePosition.y, available.x, available.y);

					auto view = ToGizmoMatrix(camera.GetViewMatrix());
					auto projection = ToGizmoMatrix(camera.GetProjectionMatrix());
					auto transform = ToGizmoMatrix(instance.worldTransform);
					const ImGuizmo::OPERATION operation = gizmoSettings->operation == GizmoOperation::Translate
						? ImGuizmo::TRANSLATE
						: gizmoSettings->operation == GizmoOperation::Rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
					const ImGuizmo::MODE mode = gizmoSettings->space == GizmoSpace::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
					if (ImGuizmo::Manipulate(view.data(), projection.data(), operation, mode, transform.data())) {
						if (!scene.SetWorldTransform(selectedInstance->index, FromGizmoMatrix(transform))) {
							ERROR_MESSAGE(log::Category::Core, "EngineViewportWindow",
								"Unable to update the selected node transform because its parent transform is singular");
						}
					}
				}
			}
		}
		ImGui::End();
	}

} // namespace Engine
