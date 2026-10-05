#include "graphics/ui/EngineViewportWindow.hpp"
#include "graphics/ImGuiSystem.hpp"
#include "graphics/ui/WindowMessages.hpp"
#include "render/ViewPort.hpp"

#include "ImGuizmo.h"
#include "imgui.h"

#include <array>

namespace elm {

	namespace {

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
		if (!m_visible) return;

		ImGui::SetNextWindowPos(ImVec2(460.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}

		const auto selectedInstance = Request<SelectedSceneInstance>();
		const auto gizmoSettings = Request<GizmoSettings>();
		const bool hasSelection = selectedInstance && selectedInstance->index < scene.instances.size();

		if (auto texture = ImGuiSystem::ToTextureId(viewPort.GetColorTexture())) {
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

				if (hasSelection && gizmoSettings && scene.instances[selectedInstance->index].visible) {
					auto& instance = scene.instances[selectedInstance->index];
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
						instance.worldTransform = FromGizmoMatrix(transform);
					}
				}
			}
		}
		ImGui::End();
	}

} // namespace Engine
