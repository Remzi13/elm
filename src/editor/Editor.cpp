#include "editor/Editor.hpp"

#include "core/JobSystem.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"
#include "core/Threading.hpp"
#include "core/Unexpected.hpp"

#include "script/ProjectPlayStart.hpp"

#include "graphics/FrameStats.hpp"
#include "graphics/debug/ColliderVisualizer.hpp"
#include "graphics/debug/PhysicsVisualizer.hpp"
#include "graphics/debug/WorldAxesVisualizer.hpp"
#include "editor/ui/EngineViewportWindow.hpp"
#include "editor/ui/LogWindow.hpp"
#include "editor/ui/ModelEditorWindow.hpp"
#include "editor/ui/ProfilerWindow.hpp"
#include "editor/ui/NodeInspectorWindow.hpp"
#include "editor/ui/SceneHierarchyWindow.hpp"
#include "editor/ui/SettingsWindow.hpp"
#include "editor/ui/SocLabWindow.hpp"

#include "editor/ProjectHubWindow.hpp"
#include "editor/ProjectWindow.hpp"

#include "platform/Process.hpp"

#include "resource/ProgramSettings.hpp"
#include "resource/SettingsFile.hpp"
#include "resource/ProjectBuilder.hpp"
#include "resource/ProjectTemplate.hpp"
#include "resource/Serializer.hpp"

#include "script/ScriptSystem.hpp"
#include "editor/script/ScriptEditorWindows.hpp"
#include "editor/script/ScriptWindow.hpp"

#include <algorithm>
#include <utility>

namespace elm {

    namespace {

        constexpr const char* LogSource = "Editor";
        constexpr StringView MainWindowTitle = "C++23 3D Engine - Direct3D 12 & Jolt Physics";
        constexpr Size MainWindowSize { 1280, 720 };

    } // namespace

    Editor::Editor()
        : m_scene(m_resources.GetModels())
        , m_graphics(m_settings, m_scene, m_resources.GetModels())
        , m_debugDraw(m_settings)
        , m_scripts({ .scene = &m_scene, .physics = &m_physics, .resources = &m_resources, .input = &m_platform.GetInput(), .camera = &m_graphics.GetViewCamera(), .settings = &m_settings, .debugDraw = &m_debugDraw.GetDebugDraw() })
        , m_cameraController(m_graphics.GetViewCamera())
        , m_editMode(m_scene, m_scripts)
        , m_edits(m_scene.EditScene())
        , m_playRequests(m_scripts.GetPlayMode().Requests())
        , m_scriptInspection(script::MakeScriptInspection(m_scripts))
        , m_imgui(MakeUnique<ImGuiSystem>())
    {
    }

    Editor::~Editor()
    {
        Shutdown();
    }

    void Editor::RegisterSettings(Settings& settings)
    {
        settings.Register(Settings::Category::Core, LastProjectSetting, "Last project", String {});
        settings.Register(Settings::Category::Core, RecentProjectsSetting, "Recent projects", String {});
    }

    auto Editor::Init() -> EngineResult<void>
    {
        RegisterSettings(m_settings);
        script::ScriptSystem::RegisterSettings(m_settings);
        if (auto loaded = resource::LoadProgramSettings(m_settings); !loaded)
            return loaded;
        core::registerThread(core::ThreadRole::Update);
        ELM_PROFILE_THREAD("Main Thread");
        core::JobSystem::Get().Init();
        if (auto platformInit = m_platform.Init(MainWindowSize, MainWindowTitle); !platformInit)
            return platformInit;
        const platform::Window& window = m_platform.GetWindow();
        if (auto graphicsInit = m_graphics.Init(window.GetNativeWindow(), window.GetFramebufferSize(), MainWindowSize); !graphicsInit) {
            m_platform.Shutdown();
            return graphicsInit;
        }
        if (auto physicsInit = m_physics.Init(); !physicsInit) {
            m_graphics.Shutdown();
            m_platform.Shutdown();
            return physicsInit;
        }
        m_scripts.GetPlayMode().SetStartSource([this](script::PlayTarget target) { return script::MakeProjectPlayStart(m_resources, target); });
        m_initialized = true;
        m_platform.GetInput().AddSubscriber(&m_cameraController, static_cast<int32_t>(InputPriority::Gameplay), "CameraController");
        if (auto init = m_imgui->Init(m_platform.GetWindow(), m_graphics.GetRender(), "Engine Debug UI"); !init)
            return init;
        CreateEditorWindows();
        m_hub = &m_imgui->EmplaceWindow<editor::ProjectHubWindow>(
            m_settings,
            editor::ProjectHubDesc {
                .recent = [this] { return RecentProjects(); },
                .open = [this](const Path& file) { m_requestedProject = file; },
                .create = [this](const Path& location, StringView name) { return CreateProject(location, name); },
                .defaultLocation = FileSystem::Resolve("projects"),
            });
        UpdateProjectMenu(nullptr);
        AddVisualizers();
        // The editor starts in Edit mode: physics and scripts wait for Play
        if (m_settings.Get<bool>(Settings::Category::Script, script::PlayMode::PlayOnStartSetting, false))
            m_scripts.GetPlayMode().RequestPlay(script::PlayTarget::Project);
        return {};
    }

    void Editor::Run()
    {
        m_clock.Start();
        while (RunFrame()) {
        }
    }

    bool Editor::RunFrame()
    {
        ELM_PROFILE_FRAME();
        const float deltaTime = m_clock.Tick();
        m_physics.ClearFrameLog();
        m_platform.PollEvents();
        // Before anything of the frame draws, physics steps included
        m_debugDraw.BeginFrame(deltaTime);
        // Before Play/Stop: a project switch stops Play itself
        ApplyProjectRequest();
        m_editMode.ApplyRequest();
        // Before Simulate, which culls from the camera. The viewport state comes from the previous UI
        // frame, the keys from the input the last Simulate dispatched
        m_cameraController.SetViewportState(m_imgui->IsSceneViewportHovered(), m_imgui->IsSceneViewportFocused());
        m_cameraController.SetEnabled(!m_scripts.GetPlayMode().IsPlaying());
        m_cameraController.Update(deltaTime);
        Simulate(deltaTime);
        // The SocLab window shows the culling depth buffer; the game never makes the texture
        m_graphics.GetCulling().UpdateDepthPreviewTexture(m_settings);
        Draw();
        return !m_platform.ShouldClose();
    }

    void Editor::Simulate(float deltaTime)
    {
        for (uint32_t step = m_clock.TakeFixedSteps(deltaTime); step > 0; --step) {
            ELM_PROFILE_SCOPE_N("FixedUpdate Physics Step");
            // Edit mode: the scene stands still
            std::span<const CollisionEvent> collisions;
            if (m_scripts.GetPlayMode().IsPlaying()) {
                m_physics.Step(m_scene.EditScene(), FrameClock::FixedStep);
                collisions = m_physics.CollisionEvents();
            }
            m_scripts.FixedUpdate(FrameClock::FixedStep, collisions);
        }
        m_platform.DispatchInput();
        m_scripts.Update(deltaTime);
        // Scripts move nodes through local transforms
        m_scene.UpdateTransforms();
        m_graphics.Cull();
    }

    void Editor::Draw()
    {
        render::FrameWriter frame = m_graphics.BeginFrame();
        if (!frame)
            return;
        BuildUi(frame);
        // The UI may have moved nodes since Simulate; graphics only reads the scene
        m_scene.UpdateTransforms();
        m_graphics.DrawWorld(frame, m_platform.GetWindow().GetFramebufferSize());
        m_debugDraw.EndFrame(frame);
        m_graphics.EndFrame(frame);
        log::flushFile();
    }

    void Editor::Shutdown()
    {
        if (!m_initialized)
            return;
        // The settings are the editor's: the player only reads them
        if (auto savedSettings = resource::WriteSettingsFile(m_settings, Settings::DefaultFilePath()); !savedSettings)
            ERROR_MESSAGE(log::Category::Core, LogSource, "%s", savedSettings.error().message.c_str());
        // The UI's surfaces and textures go before rendering, with its thread stopped
        m_graphics.StopRendering();
        m_hub = nullptr;
        m_imgui->Shutdown();
        m_platform.GetInput().RemoveSubscriber(&m_cameraController);
        // Scripts go first: they may still talk to the other systems in on_shutdown
        m_scripts.Shutdown();
        m_physics.Shutdown();
        m_scene.Clear();
        m_resources.Close();
        m_graphics.Shutdown();
        m_platform.Shutdown();
        core::JobSystem::Get().Shutdown();
        m_initialized = false;
    }

    auto Editor::OpenProject(const Path& folderOrFile) -> EngineResult<void>
    {
        auto project = resource::ResourceSystem::ReadProject(folderOrFile);
        if (!project)
            return MakeUnexpected(project.error());
        if (m_resources.GetProject()) {
            m_editMode.Stop();
            // The windows go before the data they look at
            CloseProjectWindows();
            EndProject();
        }
        StartProject(std::move(*project));
        OpenProjectWindows(*m_resources.GetProject());
        return {};
    }

    void Editor::StartProject(project::Project project)
    {
        m_resources.Open(std::move(project));
        // In Edit the main module waits for Play: the Lua state serves the script props of Scene Hierarchy and the console
        if (auto scriptInit = m_scripts.Init(m_resources.GetProject()->Scripts()); !scriptInit)
            ERROR_MESSAGE(log::Category::Script, LogSource, "%s", scriptInit.error().message.c_str());
    }

    void Editor::EndProject()
    {
        if (!m_resources.GetProject())
            return;
        m_scripts.GetPlayMode().Stop();
        // on_shutdown still sees the scene
        m_scripts.Shutdown();
        m_physics.Clear();
        m_scene.Clear();
        m_resources.Close();
    }

    void Editor::OpenProjectWindows(const project::Project& project)
    {
        const Path openedScene = OpenStartupScene(project);
        // The steps were about the scene of the project before
        m_edits.Clear();
        RememberProject(project.File());
        CreateProjectWindows(project, openedScene);
        UpdateProjectMenu(&project);
        if (m_hub)
            m_hub->SetVisible(false);
    }

    void Editor::CloseProjectWindows()
    {
        // Nothing opens in the windows that go; the nodes of the next scene are others
        m_files = {};
        m_sceneFile = {};
        m_selection = {};
        m_imgui->SetSceneMenu(nullptr, nullptr);
        m_imgui->CloseProjectWindows();
        m_sceneDocument.reset();
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
            return MakeUnexpected(ErrorCode::UnknownError, "The program " + String(player.string().c_str()) + " is not next to the editor: build the target elm", log::Category::Core, LogSource);
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
            return Path {};
        return ResolveExistingProjectPath(Path(text));
    }

    void Editor::BuildUi(render::FrameWriter& frame)
    {
        // UI logic (GLFW backend, widgets, platform windows) belongs to the main thread
        ELM_PROFILE_SCOPE_N("Build Overlay");
        m_frameStats = { m_clock.GetFps(), m_clock.GetAverageFrameMs(), m_physics.GetNumBodies() };
        // Edits of the selected node and of the structure become history steps; Play freezes it
        m_edits.SetFrozen(m_scripts.GetPlayMode().IsPlaying());
        m_edits.BeginFrame(m_selection.node);
        m_imgui->BuildFrame(frame);
        m_edits.EndFrame(m_imgui->IsWidgetHeld());
    }

    void Editor::AddVisualizers()
    {
        graphics::DebugDraw& debugDraw = m_debugDraw.GetDebugDraw();
        m_debugDraw.AddVisualizer(MakeUnique<graphics::WorldAxesVisualizer>(debugDraw));
        m_debugDraw.AddVisualizer(MakeUnique<graphics::ColliderVisualizer>(m_scene.GetScene(), debugDraw));
        m_debugDraw.AddVisualizer(MakeUnique<graphics::PhysicsVisualizer>(m_physics.FrameLog(), debugDraw));
    }

    void Editor::CreateEditorWindows()
    {
        m_imgui->SetToolbar(EditorToolbarDesc { .play = &m_playRequests, .debugDraw = &m_debugDraw.GetDebugDraw() });
        m_imgui->AddWindow(MakeUnique<SocLabWindow>(m_settings, &m_graphics.GetCulling()));
        m_imgui->AddWindow(MakeUnique<EngineViewportWindow>(m_settings, EngineViewportDeps { m_scene.EditScene(), m_graphics.GetViewCamera(), m_graphics.GetViewPort(), m_debugDraw.GetDebugDraw(), m_playRequests, m_selection, m_gizmo, m_edits, m_sceneFile }));
        m_imgui->AddWindow(MakeUnique<LogWindow>(m_settings));
        m_imgui->AddWindow(MakeUnique<ProfilerWindow>(m_settings, ProfilerDeps { m_graphics.GetRender(), m_frameStats }));
        m_imgui->AddWindow(MakeUnique<SettingsWindow>(m_settings));
    }

    void Editor::CreateProjectWindows(const project::Project& project, const Path& openedScene)
    {
        Scene& scene = m_scene.EditScene();
        ModelCatalog& models = m_resources.GetModels();
        m_sceneDocument.emplace(SceneDocumentDeps { scene, models, m_edits, m_selection, m_playRequests }, project.Assets(), project.Scenes(), openedScene);
        m_sceneFile = SceneFileRequests { .save = [this] { (void)m_sceneDocument->Save(); } };
        m_imgui->SetSceneMenu(&*m_sceneDocument, &m_edits);
        auto hierarchy = MakeUnique<SceneHierarchyWindow>(m_settings,
            SceneHierarchyDeps { scene, models, m_edits, *m_sceneDocument, m_selection, m_sceneFile, m_graphics.GetViewCamera(), m_playRequests }, project.Assets(), project.Models());
        auto inspector = MakeUnique<NodeInspectorWindow>(m_settings,
            NodeInspectorDeps { scene, models, m_edits, *m_sceneDocument, m_selection, m_gizmo, m_playRequests, m_scriptInspection, m_files, m_sceneFile },
            NodeInspectorWindow::Folders { project.Assets(), project.Models(), project.Scripts() });
        auto modelEditor = MakeUnique<ModelEditorWindow>(m_settings,
            ModelEditorDeps { m_graphics.GetRender().Resources(), scene, models, m_graphics.GetViewCamera() }, project.Assets(), project.Models());
        auto scripts = script::CreateScriptWindow(m_scripts, m_settings);
        // The windows that open files: destroyed with the project windows, emptied before (CloseProjectWindows)
        m_files = FileOpener {
            .scene = [this](const Path& file) { m_imgui->OpenSceneFile(file); },
            .model = [window = modelEditor.get()](const Path& file) { window->OpenModelFile(file); },
            .sceneModel = [window = modelEditor.get()](ModelHandler model) { window->OpenSceneModel(model); },
            .script = [window = scripts.get()](const Path& file) { window->OpenFile(file); },
        };
        m_imgui->AddProjectWindow(std::move(hierarchy));
        m_imgui->AddProjectWindow(std::move(inspector));
        m_imgui->AddProjectWindow(std::move(modelEditor));
        m_imgui->AddProjectWindow(MakeUnique<editor::ProjectWindow>(m_settings, project, m_files,
            [this](project::ProjectEntry entry) { return SetProjectEntry(std::move(entry)); }));
        m_imgui->AddProjectWindow(std::move(scripts));
    }

    void Editor::UpdateProjectMenu(const project::Project* project)
    {
        ProjectMenuDesc menu { .fileExtension = String(project::Project::FileExtension),
            .open = [this](const Path& file) { m_requestedProject = file; },
            .recent = RecentProjects(),
            .showHub = [this] { ShowProjectHub(); },
            .runStandalone = [this] { RunStandalone(); },
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
        (void)resource::WriteSettingsFile(m_settings, Settings::DefaultFilePath());
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
        return {};
    }

    auto Editor::SetProjectEntry(project::ProjectEntry entry) -> EngineResult<void>
    {
        // The file first: a project that cannot be saved keeps the entry it had
        project::Project changed = *m_resources.GetProject();
        changed.SetEntry(entry);
        if (auto saved = resource::Serializer::SaveProject(changed); !saved)
            return saved;
        m_resources.SetEntry(std::move(entry));
        return {};
    }

    void Editor::RunStandalone()
    {
        const project::Project* project = m_resources.GetProject();
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
        const project::Project* project = m_resources.GetProject();
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
            return {};
        // A broken scene file is logged; the editor opens with an empty scene
        auto scene = m_resources.LoadStartupScene();
        if (!scene)
            return {};
        (void)m_scene.Append(std::move(*scene));
        return project.Resolve(*project.Desc().entry.startupScene);
    }

} // namespace elm
