#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "core/Profiling.hpp"
#include "core/Unexpected.hpp"

#include "graphics/debug/ColliderVisualizer.hpp"
#include "graphics/debug/PhysicsVisualizer.hpp"
#include "graphics/debug/WorldAxesVisualizer.hpp"
#include "editor/ui/ModelEditorWindow.hpp"
#include "editor/ui/SceneHierarchyWindow.hpp"

#include "editor/ProjectHubWindow.hpp"
#include "editor/ProjectWindow.hpp"

#include "platform/Process.hpp"

#include "resource/ProjectBuilder.hpp"
#include "resource/ProjectTemplate.hpp"
#include "resource/Serializer.hpp"

#include "script/ScriptSystem.hpp"
#include "editor/script/ScriptEditorWindows.hpp"

#include <algorithm>
#include <utility>

namespace elm {

	namespace {

		constexpr const char* LogSource = "Editor";
		constexpr StringView MainWindowTitle = "C++23 3D Engine - Direct3D 12 & Jolt Physics";
		constexpr uint32_t MainWindowWidth = 1280;
		constexpr uint32_t MainWindowHeight = 720;

	} // namespace

	Editor::Editor()
		: m_settings(m_engine.GetSettings())
		, m_debugDraw(m_settings)
		, m_cameraController(m_engine.GetGraphics().GetViewCamera())
		, m_editMode(m_engine.GetPlayMode(), m_engine.GetSceneSystem(), m_engine.GetScripts())
		, m_imgui(MakeUnique<ImGuiSystem>())
	{
	}

	Editor::~Editor()
	{
		Shutdown();
	}

	void Editor::RegisterSettings(Settings& settings)
	{
		settings.Register(Settings::Category::Core, LastProjectSetting, "Last project", String { });
		settings.Register(Settings::Category::Core, RecentProjectsSetting, "Recent projects", String { });
	}

	auto Editor::Init() -> EngineResult<void>
	{
		RegisterSettings(m_settings);
		const EngineDesc desc { MainWindowWidth, MainWindowHeight, MainWindowTitle, &m_debugDraw.GetDebugDraw() };
		if (auto engineInit = m_engine.Init(desc); !engineInit)
			return engineInit;
		m_engine.GetInput().AddSubscriber(&m_cameraController, static_cast<int32_t>(InputPriority::Gameplay), "CameraController");
		if (auto init = m_imgui->Init(m_engine.GetWindow(), m_engine.GetGraphics().GetRender(), m_settings, "Engine Debug UI", &m_engine.GetGraphics().GetCulling()); !init)
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

	void Editor::Run()
	{
		while (!m_engine.ShouldClose()) {
			const float deltaTime = m_engine.BeginFrame();
			// Before anything of the frame draws, physics steps included
			m_debugDraw.BeginFrame(deltaTime);
			// Before Play/Stop: a project switch stops Play itself
			ApplyProjectRequest();
			m_editMode.ApplyRequest();
			// Before Simulate, which culls from the camera. The viewport state comes from the previous UI
			// frame, the keys from the input the last Simulate dispatched
			m_cameraController.SetViewportState(m_imgui->IsSceneViewportHovered(), m_imgui->IsSceneViewportFocused());
			m_cameraController.SetEnabled(!m_engine.GetPlayMode().IsPlaying());
			m_cameraController.Update(deltaTime);
			m_engine.Simulate(deltaTime);
			// The SocLab window shows the culling depth buffer; the game never makes the texture
			m_engine.GetGraphics().GetCulling().UpdateDepthPreviewTexture(m_settings);

			render::FrameWriter frame = m_engine.BeginRenderFrame();
			if (!frame)
				return;
			BuildUi(frame);
			m_engine.DrawScene(frame);
			m_debugDraw.EndFrame(frame);
			m_engine.SubmitFrame(frame);
		}
	}

	void Editor::Shutdown()
	{
		if (!m_engine.IsRunning())
			return;
		// The settings are the editor's: the player only reads them
		if (auto savedSettings = m_settings.Save(Settings::DefaultFilePath()); !savedSettings)
			ERROR_MESSAGE(log::Category::Core, LogSource, "%s", savedSettings.error().message.c_str());
		// The UI's surfaces and textures go before the render system, with its thread stopped
		m_engine.StopRendering();
		m_hub = nullptr;
		m_imgui->Shutdown();
		m_engine.GetInput().RemoveSubscriber(&m_cameraController);
		m_engine.Shutdown();
	}

	auto Editor::OpenProject(const Path& folderOrFile) -> EngineResult<void>
	{
		auto project = resource::ResourceSystem::ReadProject(folderOrFile);
		if (!project)
			return MakeUnexpected(project.error());
		if (m_engine.GetResources().GetProject()) {
			m_editMode.Stop();
			CloseProjectWindows();
			m_engine.CloseProject();
		}
		m_engine.OpenProject(std::move(*project));
		OpenProjectWindows(*m_engine.GetResources().GetProject());
		return { };
	}

	void Editor::OpenProjectWindows(const project::Project& project)
	{
		const Path openedScene = OpenStartupScene(project);
		RememberProject(project.File());
		CreateProjectWindows(project, openedScene);
		UpdateProjectMenu(&project);
		if (m_hub)
			m_hub->SetVisible(false);
	}

	void Editor::CloseProjectWindows()
	{
		m_imgui->CloseProjectWindows();
		UpdateProjectMenu(nullptr);
	}

	auto Editor::FindPlayerExecutable() -> EngineResult<Path>
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

	void Editor::ShowProjectHub()
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

	Path Editor::LastProject() const
	{
		const String text = m_settings.Get<String>(Settings::Category::Core, LastProjectSetting);
		if (text.empty())
			return Path { };
		return ResolveExistingProjectPath(Path(text));
	}

	void Editor::BuildUi(render::FrameWriter& frame)
	{
		// UI logic (GLFW backend, widgets, platform windows) belongs to the main thread
		ELM_PROFILE_SCOPE_N("Build Overlay");
		m_imgui->BuildFrame({ m_engine.GetGraphics().GetRender(), m_engine.GetScene(), m_engine.GetResources().GetModels(), m_engine.GetGraphics().GetViewCamera(),
			m_engine.GetFrameStats(), m_engine.GetGraphics().GetViewPort(), frame, m_debugDraw.GetDebugDraw() });
	}

	void Editor::AddVisualizers()
	{
		graphics::DebugDraw& debugDraw = m_debugDraw.GetDebugDraw();
		m_debugDraw.AddVisualizer(MakeUnique<graphics::WorldAxesVisualizer>(debugDraw));
		m_debugDraw.AddVisualizer(MakeUnique<graphics::ColliderVisualizer>(m_engine.GetScene(), debugDraw));
		m_debugDraw.AddVisualizer(MakeUnique<graphics::PhysicsVisualizer>(m_engine.GetPhysics().FrameLog(), debugDraw));
	}

	void Editor::CreateProjectWindows(const project::Project& project, const Path& openedScene)
	{
		const SceneHierarchyWindow::Folders folders { project.Assets(), project.Scenes(), project.Models(), project.Scripts() };
		m_imgui->AddProjectWindow(MakeUnique<SceneHierarchyWindow>(m_settings, folders, openedScene));
		m_imgui->AddProjectWindow(MakeUnique<ModelEditorWindow>(m_settings, project.Assets(), project.Models()));
		m_imgui->AddProjectWindow(MakeUnique<editor::ProjectWindow>(m_settings, project,
			[this](project::ProjectEntry entry) { return SetProjectEntry(std::move(entry)); }));
		for (auto& window : script::CreateEditorWindows(m_engine.GetScripts(), m_settings))
			m_imgui->AddProjectWindow(std::move(window));
	}

	void Editor::UpdateProjectMenu(const project::Project* project)
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

	Vector<Path> Editor::RecentProjects() const
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

	void Editor::RememberProject(const Path& file)
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

	void Editor::ApplyProjectRequest()
	{
		if (!m_requestedProject)
			return;
		const Path requested = std::move(*m_requestedProject);
		m_requestedProject.reset();
		// The error is logged; the open project stays
		(void)OpenProject(requested);
	}

	auto Editor::CreateProject(const Path& location, StringView name) -> EngineResult<void>
	{
		// The new project's scripts get the LuaLS description of the engine API
		auto luaApi = script::ScriptSystem::GenerateApiMeta();
		if (!luaApi)
			return MakeUnexpected(luaApi.error());
		auto file = resource::ProjectTemplate::Create(FileSystem::Resolve(location), name, *luaApi);
		if (!file)
			return MakeUnexpected(file.error());
		m_requestedProject = std::move(*file);
		return { };
	}

	auto Editor::SetProjectEntry(project::ProjectEntry entry) -> EngineResult<void>
	{
		// The file first: a project that cannot be saved keeps the entry it had
		project::Project changed = *m_engine.GetResources().GetProject();
		changed.SetEntry(entry);
		if (auto saved = resource::Serializer::SaveProject(changed); !saved)
			return saved;
		m_engine.GetResources().SetEntry(std::move(entry));
		return { };
	}

	void Editor::RunStandalone()
	{
		const project::Project* project = m_engine.GetResources().GetProject();
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

	void Editor::BuildGame()
	{
		const project::Project* project = m_engine.GetResources().GetProject();
		if (!project)
			return;
		// Errors are logged: the editor goes on
		const auto executable = FindPlayerExecutable();
		if (!executable)
			return;
		const auto folder = resource::ProjectBuilder::Build(*project, { *executable, FileSystem::Resolve("shaders") });
		if (folder)
			LOG_MESSAGE(log::Category::Core, LogSource, "Build Game: %s is in %s", project->Name().c_str(), folder->string().c_str());
	}

	Path Editor::OpenStartupScene(const project::Project& project)
	{
		if (!project.Desc().entry.startupScene)
			return { };
		// A broken scene file is logged; the editor opens with an empty scene
		auto scene = m_engine.GetResources().LoadStartupScene();
		if (!scene)
			return { };
		(void)m_engine.GetSceneSystem().Append(std::move(*scene));
		return project.Resolve(*project.Desc().entry.startupScene);
	}

} // namespace elm
