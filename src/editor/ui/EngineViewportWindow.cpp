#include "editor/ui/EngineViewportWindow.hpp"

#include "editor/ui/SceneEditShortcuts.hpp"

#include "core/Log.hpp"
#include "graphics/Camera.hpp"
#include "graphics/debug/DebugDraw.hpp"
#include "render/ViewPort.hpp"
#include "Scene/ModelComponent.hpp"
#include "Scene/Scene.hpp"

#include "imgui.h"

#include <span>

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

		/// W, E, R. Not with the right mouse button: the fly camera has WASD and QE then.
		void HandleGizmoKeys(GizmoSettings& gizmo) {
			const ImGuiIO& io = ImGui::GetIO();
			if (io.WantTextInput || io.KeyCtrl || ImGui::IsMouseDown(ImGuiMouseButton_Right))
				return;
			if (ImGui::IsKeyPressed(ImGuiKey_W, false))
				gizmo.operation = GizmoOperation::Translate;
			else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
				gizmo.operation = GizmoOperation::Rotate;
			else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
				gizmo.operation = GizmoOperation::Scale;
		}

		/// Under every label, one pixel down and right: readable on light and dark scenes.
		constexpr ImU32 LabelShadowColor = IM_COL32(0, 0, 0, 200);

		/// Debug labels over the image of the view at position with size, centered on their points.
		/// A label behind the camera is not shown; the scene does not hide labels.
		void DrawDebugLabels(std::span<const graphics::DebugLabel> labels, const Camera& camera, ImVec2 position, ImVec2 size) {
			if (labels.empty())
				return;
			const Matrix4x4 viewProjection = camera.GetViewProjectionMatrix();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->PushClipRect(position, ImVec2(position.x + size.x, position.y + size.y), true);
			for (const graphics::DebugLabel& label : labels) {
				const Vector4 clip = viewProjection.TransformPoint(label.position);
				if (clip.w <= 0.0f)
					continue;
				// NDC y goes up, screen y down
				const ImVec2 textSize = ImGui::CalcTextSize(label.text.c_str());
				const float x = position.x + (clip.x / clip.w * 0.5f + 0.5f) * size.x - textSize.x * 0.5f;
				const float y = position.y + (0.5f - clip.y / clip.w * 0.5f) * size.y - textSize.y * 0.5f;
				const ImVec4 color { label.color.x, label.color.y, label.color.z, label.color.w };
				drawList->AddText(ImVec2(x + 1.0f, y + 1.0f), LabelShadowColor, label.text.c_str());
				drawList->AddText(ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(color), label.text.c_str());
			}
			drawList->PopClipRect();
		}

		/// The mouse over the image of the view at position with size, for the gizmo.
		graphics::GizmoInput ReadGizmoInput(const Camera& camera, ImVec2 position, ImVec2 size, bool overView) {
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			// Screen y goes down, NDC y up
			const float ndcX = (mouse.x - position.x) / size.x * 2.0f - 1.0f;
			const float ndcY = 1.0f - (mouse.y - position.y) / size.y * 2.0f;
			return {
				camera.GetRayThrough(ndcX, ndcY),
				camera.GetPosition(),
				overView,
				overView && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
				ImGui::IsMouseDown(ImGuiMouseButton_Left),
			};
		}

	}

	EngineViewportWindow::EngineViewportWindow(Settings& settings, const EngineViewportDeps& deps)
		: IImGuiWindow(settings)
		, m_deps(deps)
	{
	}

	void EngineViewportWindow::Render(render::FrameWriter&) {
		Scene& scene = m_deps.scene;
		Camera& camera = m_deps.camera;
		render::ViewPort& viewPort = m_deps.viewPort;
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
		if (m_focused)
			HandleSceneEditShortcuts(m_deps.edits, m_deps.sceneFile);
		// In Play the keys are the game's
		const bool playing = m_deps.play.isPlaying && m_deps.play.isPlaying();
		if (m_focused && !playing)
			HandleGizmoKeys(m_deps.gizmo);

		const Scene::Node* selected = scene.Find(m_deps.selection.node);
		// A node the user hid gets no gizmo; a node without a model is not hidden
		const auto* nodeModels = scene.FindComponents<ModelComponent>();
		const ModelComponent* selectedModel = selected && nodeModels ? nodeModels->Find(selected->id) : nullptr;
		const bool selectedShown = selected && (!selectedModel || selectedModel->visible);

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
				// A widget over the image takes the mouse: without one, dragging on it moves the window
				ImGui::SetCursorScreenPos(imagePosition);
				ImGui::InvisibleButton("##ViewportCanvas", available, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				// Popups and other windows on top of the image block the hover
				m_hovered = ImGui::IsItemHovered();
				DrawDebugLabels(m_deps.debugDraw.GetPublishedLabels(), camera, imagePosition, available);

				if (selectedShown)
					UpdateGizmo(selected->id, ReadGizmoInput(camera, imagePosition, available, m_hovered));
				else if (m_gizmo)
					m_gizmo->Reset();
			}
		}
		ImGui::End();
	}

	void EngineViewportWindow::UpdateGizmo(NodeId node, const graphics::GizmoInput& input) {
		if (!m_gizmo)
			m_gizmo.emplace(m_deps.debugDraw);
		// A gizmo switched off in the Debug menu is not there to grab either
		if (!m_deps.debugDraw.IsEnabled(m_gizmo->Channel())) {
			m_gizmo->Reset();
			return;
		}
		Scene& scene = m_deps.scene;
		const Matrix4x4& world = scene.WorldTransforms()[scene.IndexOf(node)];
		if (const auto moved = m_gizmo->Update(world, m_deps.gizmo, input); moved && !scene.SetWorldTransform(node, *moved)) {
			ERROR_MESSAGE(log::Category::Core, "EngineViewportWindow",
				"Unable to update the selected node transform because its parent transform is singular");
		}
		m_gizmo->Draw(m_deps.debugDraw);
	}

} // namespace Engine
