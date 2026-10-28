#include "EditorLayer.hpp"

#include "graphics/culling/OcclusionCullingSystem.hpp"
#include "graphics/ui/ModelEditorWindow.hpp"
#include "graphics/ui/SceneHierarchyWindow.hpp"

#include "editor/ProjectHubWindow.hpp"
#include "editor/ProjectWindow.hpp"

#include "script/ScriptSystem.hpp"

#include <algorithm>
#include <utility>

namespace elm {

	EditorLayer::EditorLayer(Settings& settings, EditorRequests requests)
		: m_settings(settings)
		, m_requests(std::move(requests))
		, m_imgui(MakeUnique<ImGuiSystem>())
	{
	}

	EditorLayer::~EditorLayer() = default;

	void EditorLayer::RegisterSettings(Settings& settings)
	{
		settings.Register(Settings::Category::Core, LastProjectSetting, "Last project", String { });
		settings.Register(Settings::Category::Core, RecentProjectsSetting, "Recent projects", String { });
	}

	auto EditorLayer::Init(platform::Window& window, render::RenderSystem& renderSystem, const OcclusionCullingSystem& culling)
		-> EngineResult<void>
	{
		if (auto init = m_imgui->Init(window, renderSystem, m_settings, "Engine Debug UI", &culling); !init)
			return init;
		m_hub = &m_imgui->EmplaceWindow<editor::ProjectHubWindow>(m_settings, editor::ProjectHubDesc {
			.recent = [this] { return RecentProjects(); },
			.open = m_requests.openProject,
			.create = m_requests.createProject,
			.defaultLocation = FileSystem::Resolve("projects"),
		});
		UpdateProjectMenu(nullptr);
		return { };
	}

	void EditorLayer::Shutdown()
	{
		m_hub = nullptr;
		m_imgui->Shutdown();
	}

	void EditorLayer::OnProjectOpened(const project::Project& project, script::ScriptSystem& scripts, const Path& openedScene)
	{
		RememberProject(project.File());
		CreateProjectWindows(project, scripts, openedScene);
		UpdateProjectMenu(&project);
		if (m_hub)
			m_hub->SetVisible(false);
	}

	void EditorLayer::OnProjectClosed()
	{
		m_imgui->CloseProjectWindows();
		UpdateProjectMenu(nullptr);
	}

	void EditorLayer::ShowProjectHub()
	{
		if (m_hub)
			m_hub->SetVisible(true);
	}

	Path EditorLayer::LastProject() const
	{
		return Path(m_settings.Get<String>(Settings::Category::Core, LastProjectSetting));
	}

	void EditorLayer::BuildFrame(const ImGuiWindowContext& context)
	{
		m_imgui->BuildFrame(context);
	}

	void EditorLayer::NotifyViewportSurfacesReleased(uint64_t count)
	{
		m_imgui->NotifyViewportSurfacesReleased(count);
	}

	bool EditorLayer::IsSceneViewportHovered() const noexcept
	{
		return m_imgui->IsSceneViewportHovered();
	}

	bool EditorLayer::IsSceneViewportFocused() const noexcept
	{
		return m_imgui->IsSceneViewportFocused();
	}

	void EditorLayer::CreateProjectWindows(const project::Project& project, script::ScriptSystem& scripts, const Path& openedScene)
	{
		const SceneHierarchyWindow::Folders folders { project.Assets(), project.Scenes(), project.Models(), project.Scripts() };
		m_imgui->AddProjectWindow(MakeUnique<SceneHierarchyWindow>(m_settings, folders, openedScene));
		m_imgui->AddProjectWindow(MakeUnique<ModelEditorWindow>(m_settings, project.Assets(), project.Models()));
		m_imgui->AddProjectWindow(MakeUnique<editor::ProjectWindow>(m_settings, project, m_requests.setEntry));
		for (auto& window : scripts.CreateEditorWindows(m_settings))
			m_imgui->AddProjectWindow(std::move(window));
	}

	void EditorLayer::UpdateProjectMenu(const project::Project* project)
	{
		ProjectMenuDesc menu { .fileExtension = String(project::Project::FileExtension), .open = m_requests.openProject,
			.recent = RecentProjects(), .showHub = [this] { ShowProjectHub(); }, .runStandalone = m_requests.runStandalone,
			.buildGame = m_requests.buildGame };
		if (project) {
			menu.name = project->Name();
			menu.file = project->File();
		}
		m_imgui->SetProjectMenu(std::move(menu));
	}

	Vector<Path> EditorLayer::RecentProjects() const
	{
		Vector<Path> projects;
		const String lines = m_settings.Get<String>(Settings::Category::Core, RecentProjectsSetting);
		size_t start = 0;
		while (start < lines.size()) {
			const size_t end = (std::min)(lines.find('\n', start), lines.size());
			if (end > start)
				projects.emplace_back(StringView(lines).substr(start, end - start));
			start = end + 1;
		}
		return projects;
	}

	void EditorLayer::RememberProject(const Path& file)
	{
		const String text(file.generic_string().c_str());
		String lines = text;
		size_t kept = 1;
		for (const Path& recent : RecentProjects()) {
			const String recentText(recent.generic_string().c_str());
			if (recentText != text && kept++ < MaxRecentProjects)
				lines += "\n" + recentText;
		}
		m_settings.Set(Settings::Category::Core, RecentProjectsSetting, std::move(lines));
		m_settings.Set(Settings::Category::Core, LastProjectSetting, text);
	}

} // namespace elm
