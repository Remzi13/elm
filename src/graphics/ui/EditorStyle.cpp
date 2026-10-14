#include "graphics/ui/EditorStyle.hpp"

#include "core/AssetPaths.hpp"

#include "imgui.h"

namespace elm {

	namespace {

		constexpr float FontSize = 16.0f;

		constexpr ImVec4 Rgba(float r, float g, float b, float a = 1.0f) {
			return ImVec4(r, g, b, a);
		}

	} // namespace

	void LoadEditorFonts() {
		auto& io = ImGui::GetIO();
		const Path fontPath = AssetPaths::Root() / "fonts" / "Roboto-Medium.ttf";
		if (FileSystem::Exists(fontPath)) {
			ImFontConfig config;
			config.OversampleH = 3;
			config.OversampleV = 2;
			if (io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), FontSize, &config))
				return;
		}
		// Without the font file the UI still works with the built-in bitmap font
		io.Fonts->AddFontDefault();
	}

	void ApplyEditorStyle() {
		ImGuiStyle& style = ImGui::GetStyle();
		style = ImGuiStyle();

		style.WindowPadding = ImVec2(8.0f, 8.0f);
		style.FramePadding = ImVec2(8.0f, 4.0f);
		style.CellPadding = ImVec2(6.0f, 3.0f);
		style.ItemSpacing = ImVec2(8.0f, 5.0f);
		style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
		style.IndentSpacing = 20.0f;
		style.ScrollbarSize = 14.0f;
		style.GrabMinSize = 10.0f;

		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupBorderSize = 1.0f;
		style.FrameBorderSize = 1.0f;
		style.TabBorderSize = 0.0f;

		// Platform windows look like OS windows, so the window corners stay square
		style.WindowRounding = 0.0f;
		style.ChildRounding = 3.0f;
		style.FrameRounding = 4.0f;
		style.PopupRounding = 4.0f;
		style.ScrollbarRounding = 6.0f;
		style.GrabRounding = 3.0f;
		style.TabRounding = 4.0f;

		style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
		style.WindowMenuButtonPosition = ImGuiDir_Left;

		const ImVec4 accent = Rgba(0.33f, 0.46f, 0.70f);
		const ImVec4 accentActive = Rgba(0.27f, 0.39f, 0.62f);
		const ImVec4 accentLight = Rgba(0.45f, 0.60f, 0.88f);

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_Text] = Rgba(0.92f, 0.93f, 0.94f);
		colors[ImGuiCol_TextDisabled] = Rgba(0.50f, 0.51f, 0.53f);
		colors[ImGuiCol_WindowBg] = Rgba(0.12f, 0.125f, 0.135f);
		colors[ImGuiCol_ChildBg] = Rgba(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_PopupBg] = Rgba(0.10f, 0.105f, 0.115f, 0.98f);
		colors[ImGuiCol_Border] = Rgba(0.38f, 0.39f, 0.42f, 0.55f);
		colors[ImGuiCol_BorderShadow] = Rgba(0.0f, 0.0f, 0.0f, 0.0f);

		colors[ImGuiCol_FrameBg] = Rgba(0.17f, 0.27f, 0.43f, 0.85f);
		colors[ImGuiCol_FrameBgHovered] = Rgba(0.24f, 0.36f, 0.56f, 0.90f);
		colors[ImGuiCol_FrameBgActive] = Rgba(0.28f, 0.41f, 0.64f, 1.00f);

		colors[ImGuiCol_TitleBg] = Rgba(0.07f, 0.075f, 0.08f);
		colors[ImGuiCol_TitleBgActive] = Rgba(0.04f, 0.045f, 0.05f);
		colors[ImGuiCol_TitleBgCollapsed] = Rgba(0.07f, 0.075f, 0.08f, 0.75f);
		colors[ImGuiCol_MenuBarBg] = Rgba(0.22f, 0.225f, 0.24f);

		colors[ImGuiCol_ScrollbarBg] = Rgba(0.10f, 0.105f, 0.11f, 0.60f);
		colors[ImGuiCol_ScrollbarGrab] = Rgba(0.31f, 0.32f, 0.34f);
		colors[ImGuiCol_ScrollbarGrabHovered] = Rgba(0.40f, 0.41f, 0.44f);
		colors[ImGuiCol_ScrollbarGrabActive] = Rgba(0.48f, 0.49f, 0.52f);

		colors[ImGuiCol_CheckMark] = Rgba(0.95f, 0.96f, 0.98f);
		colors[ImGuiCol_SliderGrab] = accentLight;
		colors[ImGuiCol_SliderGrabActive] = Rgba(0.56f, 0.70f, 0.95f);

		colors[ImGuiCol_Button] = Rgba(0.27f, 0.28f, 0.30f);
		colors[ImGuiCol_ButtonHovered] = accent;
		colors[ImGuiCol_ButtonActive] = accentActive;

		colors[ImGuiCol_Header] = Rgba(0.29f, 0.30f, 0.32f);
		colors[ImGuiCol_HeaderHovered] = accent;
		colors[ImGuiCol_HeaderActive] = accentActive;

		colors[ImGuiCol_Separator] = Rgba(0.30f, 0.31f, 0.33f);
		colors[ImGuiCol_SeparatorHovered] = accent;
		colors[ImGuiCol_SeparatorActive] = accentLight;

		colors[ImGuiCol_ResizeGrip] = Rgba(0.33f, 0.46f, 0.70f, 0.25f);
		colors[ImGuiCol_ResizeGripHovered] = Rgba(0.33f, 0.46f, 0.70f, 0.70f);
		colors[ImGuiCol_ResizeGripActive] = Rgba(0.33f, 0.46f, 0.70f, 0.95f);

		colors[ImGuiCol_Tab] = Rgba(0.16f, 0.165f, 0.175f);
		colors[ImGuiCol_TabHovered] = accent;
		colors[ImGuiCol_TabActive] = Rgba(0.27f, 0.28f, 0.30f);
		colors[ImGuiCol_TabUnfocused] = Rgba(0.12f, 0.125f, 0.135f);
		colors[ImGuiCol_TabUnfocusedActive] = Rgba(0.20f, 0.205f, 0.22f);

		colors[ImGuiCol_DockingPreview] = Rgba(0.33f, 0.46f, 0.70f, 0.60f);
		colors[ImGuiCol_DockingEmptyBg] = Rgba(0.08f, 0.085f, 0.09f);

		colors[ImGuiCol_PlotLines] = Rgba(0.61f, 0.62f, 0.64f);
		colors[ImGuiCol_PlotLinesHovered] = accentLight;
		colors[ImGuiCol_PlotHistogram] = accent;
		colors[ImGuiCol_PlotHistogramHovered] = accentLight;

		colors[ImGuiCol_TableHeaderBg] = Rgba(0.20f, 0.205f, 0.22f);
		colors[ImGuiCol_TableBorderStrong] = Rgba(0.32f, 0.33f, 0.35f);
		colors[ImGuiCol_TableBorderLight] = Rgba(0.24f, 0.25f, 0.27f);
		colors[ImGuiCol_TableRowBg] = Rgba(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_TableRowBgAlt] = Rgba(1.0f, 1.0f, 1.0f, 0.03f);

		colors[ImGuiCol_TextSelectedBg] = Rgba(0.33f, 0.46f, 0.70f, 0.45f);
		colors[ImGuiCol_DragDropTarget] = Rgba(0.95f, 0.75f, 0.25f, 0.90f);
		colors[ImGuiCol_NavHighlight] = accentLight;
		colors[ImGuiCol_NavWindowingHighlight] = Rgba(1.0f, 1.0f, 1.0f, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = Rgba(0.80f, 0.80f, 0.80f, 0.20f);
		colors[ImGuiCol_ModalWindowDimBg] = Rgba(0.0f, 0.0f, 0.0f, 0.55f);
	}

} // namespace elm
