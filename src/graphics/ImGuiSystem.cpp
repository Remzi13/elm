#include "graphics/ImGuiSystem.hpp"

#include "graphics/ui/DepthPreviewWindow.hpp"
#include "graphics/ui/EngineViewportWindow.hpp"
#include "graphics/ui/SocLabWindow.hpp"

#include "imgui_internal.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace elm {

ImGuiSystem::ImGuiSystem() = default;

ImGuiSystem::~ImGuiSystem() {
	Shutdown();
}

void ImGuiSystem::AddWindow(UniquePtr<IImGuiWindow> window) {
	if (window) m_windows.push_back(std::move(window));
}

auto ImGuiSystem::Init(const ImGuiRenderContext& renderContext, StringView title) -> EngineResult<void> {
	(void)title;
	if (auto result = m_renderer.Init(renderContext); !result) {
		return std::unexpected(result.error());
	}

	auto socLab = MakeUnique<SocLabWindow>();
	m_socLabWindow = socLab.get();
	AddWindow(std::move(socLab));
	AddWindow(MakeUnique<EngineViewportWindow>());
	AddWindow(MakeUnique<DepthPreviewWindow>());

	const auto sourceRoot = std::filesystem::path{ __FILE__ }.parent_path().parent_path().parent_path();
	m_iniFilePath = std::filesystem::exists(sourceRoot / "CMakeLists.txt")
		? (sourceRoot / "imgui.ini").string()
		: "imgui.ini";

	auto& io = ImGui::GetIO();
	io.IniFilename = m_iniFilePath.c_str();
	ImGuiSettingsHandler iniHandler;
	iniHandler.TypeName = "LabSettings";
	iniHandler.TypeHash = ImHashStr("LabSettings");
	iniHandler.UserData = this;
	iniHandler.ClearAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler) {
		auto* self = static_cast<ImGuiSystem*>(handler->UserData);
		if (self) self->m_savedSettings = {};
	};
	iniHandler.ReadInitFn = [](ImGuiContext*, ImGuiSettingsHandler* handler) {
		auto* self = static_cast<ImGuiSystem*>(handler->UserData);
		if (self) self->m_savedSettings = {};
	};
	iniHandler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) -> void* {
		return std::strcmp(name, "State") == 0 ? handler->UserData : nullptr;
	};
	iniHandler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
		auto* self = static_cast<ImGuiSystem*>(entry);
		if (!self) return;
		int value = 0;
		float floatValue = 0.0f;
		if (::sscanf(line, "FrustumCulling=%d", &value) == 1) {
			self->m_savedSettings.enableFrustum = value != 0;
		} else if (::sscanf(line, "OcclusionCulling=%d", &value) == 1) {
			self->m_savedSettings.enableOcclusion = value != 0;
		} else if (::sscanf(line, "DepthBias=%f", &floatValue) == 1) {
			self->m_savedSettings.depthBias = floatValue;
		} else if (::sscanf(line, "SOCResolution=%d", &value) == 1) {
			self->m_savedSettings.resolution = value;
		} else if (::sscanf(line, "VisualMode=%d", &value) == 1) {
			self->m_savedSettings.visualMode = value;
		} else if (::sscanf(line, "DepthFalseColor=%d", &value) == 1) {
			self->m_savedSettings.depthFalseColor = value != 0;
		} else {
			return;
		}
		self->m_savedSettings.hasLoaded = true;
	};
	iniHandler.ApplyAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler) {
		auto* self = static_cast<ImGuiSystem*>(handler->UserData);
		if (self && self->m_savedSettings.hasLoaded) self->m_pendingSettings = true;
	};
	iniHandler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer) {
		auto* self = static_cast<ImGuiSystem*>(handler->UserData);
		if (!self) return;
		buffer->append("[LabSettings][State]\n");
		buffer->appendf("FrustumCulling=%d\n", self->m_uiConfig.enableFrustumCulling ? 1 : 0);
		buffer->appendf("OcclusionCulling=%d\n", self->m_uiConfig.enableOcclusionCulling ? 1 : 0);
		buffer->appendf("DepthBias=%.6f\n", self->m_uiConfig.depthBias);
		buffer->appendf("SOCResolution=%d\n", self->m_uiConfig.resolution);
		buffer->appendf("VisualMode=%d\n", static_cast<int>(self->m_uiConfig.visualMode));
		buffer->appendf("DepthFalseColor=%d\n", self->m_uiConfig.depthPreviewFalseColor ? 1 : 0);
	};
	ImGui::AddSettingsHandler(&iniHandler);
	ImGui::LoadIniSettingsFromDisk(io.IniFilename);

	return {};
}

void ImGuiSystem::Update(const ImGuiUpdateContext& context, ImGuiConfig& config) {
	m_renderer.BeginFrame();
	config.enableFrustumCulling = context.enableFrustumCulling;
	config.enableOcclusionCulling = context.enableOcclusionCulling;
	config.depthBias = context.depthBias;
	config.visualMode = context.visualMode;
	config.resolution = m_socLabWindow ? m_socLabWindow->GetResolutionIndex() : config.resolution;
	config.depthPreviewFalseColor = context.depthPreviewFalseColor;

	if (m_pendingSettings) {
		config.enableFrustumCulling = m_savedSettings.enableFrustum;
		config.enableOcclusionCulling = m_savedSettings.enableOcclusion;
		config.depthBias = m_savedSettings.depthBias;
		config.visualMode = static_cast<VisualMode>(m_savedSettings.visualMode);
		config.depthPreviewFalseColor = m_savedSettings.depthFalseColor;
		config.resolution = m_savedSettings.resolution;
		m_socLabWindow->SetResolutionIndex(m_savedSettings.resolution);
		m_pendingSettings = false;
	}

	m_dockSpace.Update(m_windows);
	for (const auto& window : m_windows) {
		if (window && window->IsVisible()) window->Update(context, config);
	}
	m_uiConfig = config;
	ImGui::Render();
}

void ImGuiSystem::Render() {
	m_renderer.Render();
}

void ImGuiSystem::Shutdown() {
	m_renderer.Shutdown();
	m_windows.clear();
	m_socLabWindow = nullptr;
}

} // namespace elm
