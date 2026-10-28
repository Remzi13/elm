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

	namespace {

		Path NormalizeProjectPath(const Path& file)
		{
			if (file.IsRelative())
				return file;
			const Path root = FileSystem::RootPath();
			if (!root.Empty()) {
				const Path relative = file.LexicallyRelative(root);
				const std::string relStr = relative.generic_string();
				if (!relStr.empty() && !relStr.starts_with(".."))
					return Path(relStr.c_str());
			}
			return file;
		}

		Path ResolveExistingProjectPath(const Path& path)
		{
			if (path.Empty())
				return path;
			const Path resolved = FileSystem::Resolve(path);
			if (FileSystem::Exists(resolved))
				return path.IsRelative() ? path : NormalizeProjectPath(resolved);

			// Fallback for paths from other OS / machines (e.g. C:/.../projects/Race/Race.elmproject)
			const std::string text = path.generic_string();
			const size_t projPos = text.find("projects/");
			if (projPos != std::string::npos) {
				const Path relative(text.substr(projPos).c_str());
				if (FileSystem::Exists(FileSystem::Resolve(relative)))
					return relative;
			}
			return path;
		}

	} // namespace

	Path EditorLayer::LastProject() const
	{
		const String text = m_settings.Get<String>(Settings::Category::Core, LastProjectSetting);
		if (text.empty())
			return Path { };
		return ResolveExistingProjectPath(Path(text));
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
			if (end > start) {
				const Path resolved = ResolveExistingProjectPath(Path(StringView(lines).substr(start, end - start)));
				projects.emplace_back(resolved);
			}
			start = end + 1;
		}
		return projects;
	}

	void EditorLayer::RememberProject(const Path& file)
	{
		const Path normalized = NormalizeProjectPath(file);
		const String text(normalized.generic_string().c_str());
		String lines = text;
		size_t kept = 1;
		for (const Path& recent : RecentProjects()) {
			const Path recentNorm = NormalizeProjectPath(recent);
			const String recentText(recentNorm.generic_string().c_str());
			if (recentText != text && kept++ < MaxRecentProjects)
				lines += "\n" + recentText;
		}
		m_settings.Set(Settings::Category::Core, RecentProjectsSetting, std::move(lines));
		m_settings.Set(Settings::Category::Core, LastProjectSetting, text);
		(void)m_settings.Save(Settings::DefaultFilePath());
	}

} // namespace elm
