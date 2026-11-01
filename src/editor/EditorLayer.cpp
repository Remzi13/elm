#include "editor/EditorLayer.hpp"

#include "core/Log.hpp"
#include "core/Unexpected.hpp"

#include "engine/Engine.hpp"

#include "graphics/debug/ColliderVisualizer.hpp"
#include "graphics/debug/PhysicsVisualizer.hpp"
#include "graphics/debug/WorldAxesVisualizer.hpp"
#include "editor/ui/ModelEditorWindow.hpp"
#include "editor/ui/SceneHierarchyWindow.hpp"

#include "editor/ProjectHubWindow.hpp"
#include "editor/ProjectWindow.hpp"

#include "platform/Process.hpp"

#include "resmgr/ProjectBuilder.hpp"
#include "resmgr/ProjectTemplate.hpp"
#include "resmgr/Serializer.hpp"

#include "script/ScriptSystem.hpp"
#include "editor/script/ScriptEditorWindows.hpp"

#include <algorithm>
#include <utility>

namespace elm {

	namespace {

		constexpr const char* LogSource = "EditorLayer";

	} // namespace

	EditorLayer::EditorLayer(Engine& engine)
		: m_engine(engine)
		, m_settings(engine.GetSettings())
		, m_debugDraw(m_settings)
		, m_imgui(MakeUnique<ImGuiSystem>())
	{
	}

	EditorLayer::~EditorLayer() = default;

	void EditorLayer::RegisterSettings(Settings& settings)
	{
		settings.Register(Settings::Category::Core, LastProjectSetting, "Last project", String { });
		settings.Register(Settings::Category::Core, RecentProjectsSetting, "Recent projects", String { });
	}

	auto EditorLayer::Init() -> EngineResult<void>
	{
		if (auto init = m_imgui->Init(m_engine.GetWindow(), m_engine.GetRenderSystem(), m_settings, "Engine Debug UI", &m_engine.GetCulling()); !init)
			return init;
		m_hub = &m_imgui->EmplaceWindow<editor::ProjectHubWindow>(m_settings, editor::ProjectHubDesc {
			.recent = [this] { return RecentProjects(); },
			.open = [this](const Path& file) { m_requestedProject = file; },
			.create = [this](const Path& location, StringView name) { return CreateProject(location, name); },
			.defaultLocation = FileSystem::Resolve("projects"),
		});
		UpdateProjectMenu(nullptr);
		AddVisualizers();
		// The editor starts in Edit mode: physics and scripts wait for Play
		if (m_settings.Get<bool>(Settings::Category::Script, script::PlayMode::PlayOnStartSetting, false))
			m_engine.GetPlayMode().RequestPlay(script::PlayTarget::Project);
		return { };
	}

	void EditorLayer::Shutdown()
	{
		// The settings are the editor's: the player only reads them
		if (auto savedSettings = m_settings.Save(Settings::DefaultFilePath()); !savedSettings)
			ERROR_MESSAGE(log::Category::Core, LogSource, "%s", savedSettings.error().message.c_str());
		m_hub = nullptr;
		m_imgui->Shutdown();
	}

	void EditorLayer::OnProjectOpened(const project::Project& project)
	{
		const Path openedScene = OpenStartupScene(project);
		RememberProject(project.File());
		CreateProjectWindows(project, openedScene);
		UpdateProjectMenu(&project);
		if (m_hub)
			m_hub->SetVisible(false);
	}

	void EditorLayer::OnProjectClosed()
	{
		m_imgui->CloseProjectWindows();
		UpdateProjectMenu(nullptr);
	}

	auto EditorLayer::FindPlayerExecutable() -> EngineResult<Path>
	{
		const auto editor = platform::CurrentExecutable();
		if (!editor)
			return MakeUnexpected(editor.error());
		Path player = editor->Parent() / PlayerProgram;
		player += editor->Extension();
		if (!FileSystem::Exists(player))
			return MakeUnexpected(ErrorCode::UnknownError, "The program " + String(player.string().c_str())
				+ " is not next to the editor: build the target elm", log::Category::Core, LogSource);
		return player;
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

	void EditorLayer::Update()
	{
		// The viewport state comes from the previous UI frame
		m_engine.GetCameraController().SetViewportState(m_imgui->IsSceneViewportHovered(), m_imgui->IsSceneViewportFocused());
		ApplyProjectRequest();
	}

	void EditorLayer::BuildOverlay(render::FrameWriter& frame)
	{
		m_imgui->BuildFrame({ m_engine.GetRenderSystem(), m_engine.GetScene(), m_engine.GetModels(), m_engine.GetCamera(),
			m_engine.GetFrameStats(), m_engine.GetViewPort(), frame, m_debugDraw.GetDebugDraw() });
	}

	void EditorLayer::RequestStop()
	{
		m_engine.GetPlayMode().RequestStop();
	}

	void EditorLayer::OnFrameRendered(const render::RenderedFrameInfo& info)
	{
		m_imgui->NotifyViewportSurfacesReleased(info.releasedSurfaceCount);
	}

	void EditorLayer::AddVisualizers()
	{
		graphics::DebugDraw& debugDraw = m_debugDraw.GetDebugDraw();
		m_debugDraw.AddVisualizer(MakeUnique<graphics::WorldAxesVisualizer>(debugDraw));
		m_debugDraw.AddVisualizer(MakeUnique<graphics::ColliderVisualizer>(m_engine.GetScene(), debugDraw));
		m_debugDraw.AddVisualizer(MakeUnique<graphics::PhysicsVisualizer>(m_engine.GetPhysics().FrameLog(), debugDraw));
	}

	void EditorLayer::CreateProjectWindows(const project::Project& project, const Path& openedScene)
	{
		const SceneHierarchyWindow::Folders folders { project.Assets(), project.Scenes(), project.Models(), project.Scripts() };
		m_imgui->AddProjectWindow(MakeUnique<SceneHierarchyWindow>(m_settings, folders, openedScene));
		m_imgui->AddProjectWindow(MakeUnique<ModelEditorWindow>(m_settings, project.Assets(), project.Models()));
		m_imgui->AddProjectWindow(MakeUnique<editor::ProjectWindow>(m_settings, project,
			[this](project::ProjectEntry entry) { return SetProjectEntry(std::move(entry)); }));
		for (auto& window : script::CreateEditorWindows(m_engine.GetScripts(), m_settings))
			m_imgui->AddProjectWindow(std::move(window));
	}

	void EditorLayer::UpdateProjectMenu(const project::Project* project)
	{
		ProjectMenuDesc menu { .fileExtension = String(project::Project::FileExtension),
			.open = [this](const Path& file) { m_requestedProject = file; }, .recent = RecentProjects(),
			.showHub = [this] { ShowProjectHub(); }, .runStandalone = [this] { RunStandalone(); },
			.buildGame = [this] { BuildGame(); } };
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

	void EditorLayer::ApplyProjectRequest()
	{
		if (!m_requestedProject)
			return;
		const Path requested = std::move(*m_requestedProject);
		m_requestedProject.reset();
		// The error is logged; the open project stays
		(void)m_engine.OpenProject(requested);
	}

	auto EditorLayer::CreateProject(const Path& location, StringView name) -> EngineResult<void>
	{
		// The new project's scripts get the LuaLS description of the engine API
		auto luaApi = script::ScriptSystem::GenerateApiMeta();
		if (!luaApi)
			return MakeUnexpected(luaApi.error());
		auto file = resmgr::ProjectTemplate::Create(FileSystem::Resolve(location), name, *luaApi);
		if (!file)
			return MakeUnexpected(file.error());
		m_requestedProject = std::move(*file);
		return { };
	}

	auto EditorLayer::SetProjectEntry(project::ProjectEntry entry) -> EngineResult<void>
	{
		// The file first: a project that cannot be saved keeps the entry it had
		project::Project changed = *m_engine.OpenedProject();
		changed.SetEntry(entry);
		if (auto saved = resmgr::Serializer::SaveProject(changed); !saved)
			return saved;
		m_engine.SetProjectEntry(std::move(entry));
		return { };
	}

	void EditorLayer::RunStandalone()
	{
		const project::Project* project = m_engine.OpenedProject();
		if (!project)
			return;
		// Errors are logged: the editor goes on
		const auto executable = FindPlayerExecutable();
		if (!executable)
			return;
		const Vector<String> arguments { "--project", String(project->File().string().c_str()) };
		if (platform::StartProcess(*executable, arguments))
			LOG_MESSAGE(log::Category::Core, LogSource, "Run Standalone: %s plays from its saved files", project->Name().c_str());
	}

	void EditorLayer::BuildGame()
	{
		const project::Project* project = m_engine.OpenedProject();
		if (!project)
			return;
		// Errors are logged: the editor goes on
		const auto executable = FindPlayerExecutable();
		if (!executable)
			return;
		const auto folder = resmgr::ProjectBuilder::Build(*project, { *executable, FileSystem::Resolve("shaders") });
		if (folder)
			LOG_MESSAGE(log::Category::Core, LogSource, "Build Game: %s is in %s", project->Name().c_str(), folder->string().c_str());
	}

	Path EditorLayer::OpenStartupScene(const project::Project& project)
	{
		if (!project.Desc().entry.startupScene)
			return { };
		// A broken scene file is logged; the editor opens with an empty scene
		auto scene = m_engine.LoadStartupScene();
		if (!scene)
			return { };
		const auto handlers = m_engine.GetModels().InsertAll(std::move(scene->models), scene->modelFiles);
		(void)m_engine.GetScene().Append(std::move(*scene), handlers);
		return project.Resolve(*project.Desc().entry.startupScene);
	}

} // namespace elm
