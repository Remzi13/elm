#include "graphics/ui/EngineViewportWindow.hpp"

#include "core/Log.hpp"
#include "graphics/debug/DebugDraw.hpp"
#include "render/ViewPort.hpp"
#include "Scene/ModelComponent.hpp"

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

		constexpr const char* DebugChannelsPopup = "DebugChannels";
		const ImVec4 PlayingColor { 1.0f, 0.62f, 0.25f, 1.0f };

		/// Play Project and Play Scene in Edit mode, Stop in Play mode. F5 plays the project, Shift+F5 the
		/// edited scene, either stops.
		void DrawPlayControls(const PlayControls& play) {
			const ImGuiIO& io = ImGui::GetIO();
			// Ctrl+F5 is Run Standalone of the Project menu
			const bool f5 = !io.WantTextInput && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F5, false);
			// The buttons come first: they are drawn whether a key was pressed or not
			if (play.playing) {
				if ((ImGui::Button("Stop") || f5) && play.stop)
					play.stop();
				ImGui::SameLine();
				ImGui::TextColored(PlayingColor, "Playing: changes are undone on Stop (F5)");
				return;
			}
			if ((ImGui::Button("Play Project") || (f5 && !io.KeyShift)) && play.playProject)
				play.playProject();
			ImGui::SameLine();
			if ((ImGui::Button("Play Scene") || (f5 && io.KeyShift)) && play.playScene)
				play.playScene();
			ImGui::SameLine();
			ImGui::TextDisabled("Edit mode (F5 project, Shift+F5 scene)");
		}

		/// Checkbox per debug draw channel, grouped by the part of the name before the first '/'.
		void DrawDebugChannelsPopup(graphics::DebugDraw& debugDraw) {
			if (!ImGui::BeginPopup(DebugChannelsPopup))
				return;
			String group;
			for (const auto& channel : debugDraw.GetChannels()) {
				const String channelGroup = channel.name.substr(0, channel.name.find('/'));
				if (channelGroup != group) {
					ImGui::SeparatorText(channelGroup.c_str());
					group = channelGroup;
				}
				bool enabled = channel.enabled;
				ImGui::PushID(channel.name.c_str());
				if (ImGui::Checkbox(channel.label.c_str(), &enabled))
					debugDraw.SetEnabled(channel.channel, enabled);
				ImGui::PopID();
			}
			ImGui::EndPopup();
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

		// Edit / Play mode, provided by the Scripts window
		if (const auto play = Request<PlayControls>()) {
			DrawPlayControls(*play);
			ImGui::SameLine();
		}
		if (ImGui::Button("Debug"))
			ImGui::OpenPopup(DebugChannelsPopup);
		DrawDebugChannelsPopup(context.debugDraw);

		const auto selectedInstance = Request<SelectedSceneInstance>();
		const auto gizmoSettings = Request<GizmoSettings>();
		const Scene::Node* selected = selectedInstance ? scene.Find(selectedInstance->node) : nullptr;
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
				DrawDebugLabels(context.debugDraw.GetPublishedLabels(), camera, imagePosition, available);

				if (selectedShown && gizmoSettings)
					UpdateGizmo(context, selected->id, *gizmoSettings, ReadGizmoInput(camera, imagePosition, available, m_hovered));
				else if (m_gizmo)
					m_gizmo->Reset();
			}
		}
		ImGui::End();
	}

	void EngineViewportWindow::UpdateGizmo(ImGuiWindowContext& context, NodeId node, const GizmoSettings& settings,
		const graphics::GizmoInput& input) {
		if (!m_gizmo)
			m_gizmo.emplace(context.debugDraw);
		// A gizmo switched off in the Debug menu is not there to grab either
		if (!context.debugDraw.IsEnabled(m_gizmo->Channel())) {
			m_gizmo->Reset();
			return;
		}
		Scene& scene = context.scene;
		const Matrix4x4& world = scene.WorldTransforms()[scene.IndexOf(node)];
		if (const auto moved = m_gizmo->Update(world, settings, input); moved && !scene.SetWorldTransform(node, *moved)) {
			ERROR_MESSAGE(log::Category::Core, "EngineViewportWindow",
				"Unable to update the selected node transform because its parent transform is singular");
		}
		m_gizmo->Draw(context.debugDraw);
	}

} // namespace Engine
