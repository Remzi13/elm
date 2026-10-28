#include "EngineApp.hpp"

#include "core/Timer.hpp"
#include "core/Profiling.hpp"
#include "core/Log.hpp"
#include "core/Unexpected.hpp"

#include "core/JobSystem.hpp"
#include "core/Threading.hpp"

#include "Scene/ModelComponent.hpp"

#include "resmgr/Serializer.hpp"

#include "graphics/GameView.hpp"
#include "graphics/debug/ColliderVisualizer.hpp"
#include "graphics/debug/PhysicsVisualizer.hpp"
#include "graphics/debug/WorldAxesVisualizer.hpp"

#include "platform/Process.hpp"

#include "resmgr/ProjectBuilder.hpp"
#include "resmgr/ProjectTemplate.hpp"

#include <iostream>
#include <utility>

namespace elm {

namespace {

    constexpr const char* LogSource = "EngineApp";

    /// The project of a project folder or a .elmproject file; a relative path starts at the engine root.
    auto LoadProject(const Path& folderOrFile) -> EngineResult<project::Project>
    {
        const Path file = project::Project::FindFile(FileSystem::Resolve(folderOrFile));
        auto loaded = resmgr::Serializer::LoadProject(file);
        if (loaded && StringView(loaded->Name()) != StringView(file.Stem().string()))
            WARNING_MESSAGE(log::Category::Core, LogSource, "Project \"%s\" is in %s: the name and the file should match",
                loaded->Name().c_str(), file.Filename().string().c_str());
        return loaded;
    }

} // namespace

EngineApp::EngineApp(AppMode mode)
    : m_renderSystem(MakeUnique<render::RenderSystem>())
    , m_physicsSystem(MakeUnique<PhysicsSystem>())
    , m_inputSystem(MakeUnique<InputSystem>())
    , m_scriptSystem(MakeUnique<script::ScriptSystem>())
    , m_cameraController(m_camera)
    , m_models(m_renderSystem->Resources())
    , m_playMode(m_scene, &m_models, m_physicsSystem.get(), *m_scriptSystem)
    , m_debugDraw(m_settings)
{
    if (mode == AppMode::Editor)
        m_editor = MakeUnique<EditorLayer>(m_settings, MakeEditorRequests());
}

EngineApp::~EngineApp()
{
    Shutdown();
}

auto EngineApp::Init(uint32_t width, uint32_t height, StringView title) -> EngineResult<void>
{ 
 
    LOG_MESSAGE( log::Category::Core, "EngineApp", "Initializing 3D Engine Core (C++23)..." );    

    FileSystem::RegisterSettings(m_settings);
    if (m_editor)
        EditorLayer::RegisterSettings(m_settings);
    m_cullingSystem.RegisterSettings(m_settings);
    script::ScriptSystem::RegisterSettings(m_settings);
    script::PlayMode::RegisterSettings(m_settings);
    const Path settingsPath = Settings::DefaultFilePath();
    if (auto loadedSettings = m_settings.Load(settingsPath); !loadedSettings)
        return MakeUnexpected(loadedSettings.error());
    FileSystem::ApplySettings(m_settings, settingsPath.Parent());
    if (m_editor)
        AddEditorVisualizers();

    m_camera.SetAspect(static_cast<float>(width) / static_cast<float>(height));
    // Initialize the render backend and its command queue first.
    core::registerThread(core::ThreadRole::Update);
    core::JobSystem::Get().Init();
    if (auto windowInit = m_window.Create(Size(width, height), title); !windowInit) {
        return MakeUnexpected(windowInit.error());
    }
    auto renderInit = m_renderSystem->Init(m_window.GetNativeWindow(), m_window.GetFramebufferSize());
    if (!renderInit) {
        m_window.Destroy();
        return MakeUnexpected(renderInit.error());
    }

    m_engineViewPort = render::ViewPort(m_renderSystem->Resources(), Size(width, height));
    m_inputSystem->AttachWindow(m_window.GetHandle());
    m_inputSystem->AddSubscriber(&m_cameraController, static_cast<int32_t>(InputPriority::Gameplay), "CameraController");

    if (auto editorInit = m_editor ? m_editor->Init(m_window, *m_renderSystem, m_cullingSystem) : EngineResult<void> { }; !editorInit) {
        m_engineViewPort = {};
        m_renderSystem->Shutdown();
        return MakeUnexpected(editorInit.error());
    }

    // Register UI input consumer for keyboard focus (e.g. typing in text fields)
    //m_inputSystem->AddListener([](InputEvent& event) -> bool {
    //    ImGuiIO& io = ImGui::GetIO();
    //    if (event.type == InputEventType::Key || event.type == InputEventType::Character) {
    //        if (io.WantCaptureKeyboard) {
    //            return true;
    //        }
    //    }
    //    return false;
    //},
    //    static_cast<int32_t>(InputPriority::UI), "ImGuiKeyboardFilter");

    // Register default AAA Action & Axis Mappings (Unreal Engine Enhanced Input style)
    m_inputSystem->AddAxisMapping("MoveForward", Key::W, 1.0f);
    m_inputSystem->AddAxisMapping("MoveForward", Key::S, -1.0f);
    m_inputSystem->AddAxisMapping("MoveRight", Key::D, 1.0f);
    m_inputSystem->AddAxisMapping("MoveRight", Key::A, -1.0f);
    m_inputSystem->AddAxisMapping("MoveUp", Key::E, 1.0f);
    m_inputSystem->AddAxisMapping("MoveUp", Key::Q, -1.0f);

    // Initialize Physics System
    auto physicsInit = m_physicsSystem->Init();
    if (!physicsInit) {
        if (m_editor)
            m_editor->Shutdown();
        m_engineViewPort = {};
        m_renderSystem->Shutdown();
        return MakeUnexpected(physicsInit.error());
    }

    m_cullingSystem.Init(m_renderSystem->Resources(), m_settings);

    // The scripts start with the project (OpenProject), the main module with Play
    m_playMode.SetStartSource([this](script::PlayTarget target) { return MakePlayStart(target); });

    m_isRunning = true;
    
    LOG_MESSAGE( log::Category::Core, "EngineApp", "Engine initialization completed successfully." );
    
    return { };
}


// ─────────────────────────────────────────────────────────────────────────────
// Render thread — draws the frame snapshots submitted by the update (main) thread.
// ─────────────────────────────────────────────────────────────────────────────
void EngineApp::RenderThreadFunc()
{
    ELM_PROFILE_THREAD("Render Thread");
    core::registerThread(core::ThreadRole::Render);

    render::RenderedFrameInfo info;
    while (m_renderSystem->RenderNextFrame(info)) {
        if (m_editor)
            m_editor->NotifyViewportSurfacesReleased(info.releasedSurfaceCount);
    }

    core::unregisterThread(core::ThreadRole::Render);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop — runs on the main thread (required for GLFW event polling).
// ─────────────────────────────────────────────────────────────────────────────
auto EngineApp::Run() -> EngineResult<void>
{
    if (!m_isRunning) {
        return MakeUnexpected(ErrorCode::UnknownError,
            "EngineApp::Run called without prior successful initialization", log::Category::Core, "EngineApp");
    }

    LOG_MESSAGE( log::Category::Core, "EngineApp", "Entering main loop with pipelined Update/Render." );
    
    ELM_PROFILE_THREAD("Main Thread");

    // The editor starts in Edit mode: physics and scripts wait for Play. The player plays the project.
    if (!m_editor || m_settings.Get<bool>(Settings::Category::Script, script::PlayMode::PlayOnStartSetting, false))
        m_playMode.RequestPlay(script::PlayTarget::Project);

    // Start the render thread
    m_renderThread = std::thread(&EngineApp::RenderThreadFunc, this);

    auto lastTime = core::getTimeStamp();
    float accumulator = 0.0f;

    while (m_isRunning && !m_quitRequested && !m_window.ShouldClose()) {
        ELM_PROFILE_FRAME();
        ELM_PROFILE_SCOPE_N("Main Thread Loop");

        auto currentTime = core::getTimeStamp();
        float deltaTime = static_cast<float>(core::getMilliseconds(lastTime, currentTime)) / 1000.f;
        lastTime = currentTime;

        // Cap maximum deltaTime to prevent physics spiral of death
        if (deltaTime > 0.25f) {
            deltaTime = 0.25f;
        }

        accumulator += deltaTime;

        // Before anything of this frame draws debug primitives, physics steps included
        m_debugDraw.BeginFrame(deltaTime);
        m_physicsSystem->ClearFrameLog();

        // ── 1. Process input (must be on main thread for GLFW) ────────
        {
            ELM_PROFILE_SCOPE_N("Process Input");
            m_inputSystem->BeginFrame();
        }

        // ── 2. Fixed Timestep Physics Update ──────────────────────────
        while (accumulator >= m_fixedTimeStep) {
            FixedUpdate(m_fixedTimeStep);
            accumulator -= m_fixedTimeStep;
        }

        // ── 3. Variable-rate game logic update ────────────────────────
        Update(deltaTime);

        // Update depth preview texture on the main thread
        {
            ELM_PROFILE_SCOPE_N("Update Depth Preview Texture");
            m_cullingSystem.UpdateDepthPreviewTexture(m_settings);
        }

        // ── 4. Build and submit the frame snapshot ────────────────────
        // Blocks only when the render thread is two frames behind
        auto frame = m_renderSystem->BeginFrame();
        if (!frame)
            break;

        BuildOverlay(frame);
        SubmitFrame(frame);
    }

    // ── Signal render thread to exit ──────────────────────────────────────
    m_renderSystem->StopRendering();
    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }

    std::cout << "[EngineApp] Main loop exited." << std::endl;
    return { };
}

void EngineApp::LoadTestScene(ScenePreset preset, uint32_t instanceCount)
{
    TestScenes::BuildScene(preset, instanceCount, m_scene, m_models);
}

auto EngineApp::OpenProject(const Path& folderOrFile) -> EngineResult<void>
{
    ELM_ASSERT(!m_project && "another project is opened through the Project menu");
    auto project = LoadProject(folderOrFile);
    if (!project)
        return MakeUnexpected(project.error());
    StartProject(std::move(*project));
    return { };
}

Path EngineApp::LastProject() const
{
    return m_editor ? m_editor->LastProject() : Path { };
}

void EngineApp::ShowProjectHub()
{
    if (m_editor)
        m_editor->ShowProjectHub();
}

EditorRequests EngineApp::MakeEditorRequests()
{
    return {
        .openProject = [this](const Path& file) { m_requestedProject = file; },
        .createProject = [this](const Path& location, StringView name) { return CreateProject(location, name); },
        .setEntry = [this](project::ProjectEntry entry) { return SetProjectEntry(std::move(entry)); },
        .runStandalone = [this] { RunStandalone(); },
        .buildGame = [this] { BuildGame(); },
    };
}

void EngineApp::AddEditorVisualizers()
{
    graphics::DebugDraw& debugDraw = m_debugDraw.GetDebugDraw();
    m_debugDraw.AddVisualizer(MakeUnique<graphics::WorldAxesVisualizer>(debugDraw));
    m_debugDraw.AddVisualizer(MakeUnique<graphics::ColliderVisualizer>(m_scene, debugDraw));
    m_debugDraw.AddVisualizer(MakeUnique<graphics::PhysicsVisualizer>(m_physicsSystem->FrameLog(), debugDraw));
}

script::PlayRequests EngineApp::MakePlayRequests()
{
    script::PlayRequests requests = m_playMode.Requests();
    // The player has no Edit mode to go back to
    if (!m_editor)
        requests.stop = [this] { m_quitRequested = true; };
    return requests;
}

void EngineApp::RunStandalone()
{
    if (!m_project)
        return;
    // Errors are logged: the editor goes on
    const auto executable = platform::CurrentExecutable();
    if (!executable)
        return;
    const Vector<String> arguments { "--project", String(m_project->File().string().c_str()), "--player" };
    if (platform::StartProcess(*executable, arguments))
        LOG_MESSAGE(log::Category::Core, LogSource, "Run Standalone: %s plays from its saved files", m_project->Name().c_str());
}

auto EngineApp::SetProjectEntry(project::ProjectEntry entry) -> EngineResult<void>
{
    // The file first: a project that cannot be saved keeps the entry it had
    project::Project changed = *m_project;
    changed.SetEntry(entry);
    if (auto saved = resmgr::Serializer::SaveProject(changed); !saved)
        return saved;
    m_project->SetEntry(std::move(entry));
    return { };
}

auto EngineApp::CreateProject(const Path& location, StringView name) -> EngineResult<void>
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

void EngineApp::ApplyProjectRequest()
{
    if (!m_requestedProject)
        return;
    const Path requested = std::move(*m_requestedProject);
    m_requestedProject.reset();
    // The error is logged; the open project stays
    auto project = LoadProject(requested);
    if (!project)
        return;
    if (m_project)
        CloseProject();
    StartProject(std::move(*project));
}

void EngineApp::CloseProject()
{
    if (m_playMode.IsPlaying())
        m_playMode.Stop();
    // on_shutdown still sees the scene
    m_scriptSystem->Shutdown();
    if (m_editor)
        m_editor->OnProjectClosed();
    m_physicsSystem->Clear();
    m_scene.Clear();
    m_models.Clear();
    LOG_MESSAGE(log::Category::Core, LogSource, "Project %s closed", m_project->Name().c_str());
    m_project.reset();
}

void EngineApp::StartProject(project::Project project)
{
    m_project = std::move(project);

    const script::EngineContext scriptEngine { &m_scene, &m_models, m_inputSystem.get(), &m_camera, &m_settings, m_physicsSystem.get(),
        MakePlayRequests(), &m_debugDraw.GetDebugDraw(), &*m_project };
    // In Edit the main module waits for Play: the Lua state serves the script props of Scene Hierarchy and the console
    if (auto scriptInit = m_scriptSystem->Init(scriptEngine, m_project->Scripts()); !scriptInit)
        ERROR_MESSAGE(log::Category::Script, LogSource, "%s", scriptInit.error().message.c_str());
    if (m_editor) {
        const Path openedScene = OpenStartupScene();
        m_editor->OnProjectOpened(*m_project, *m_scriptSystem, openedScene);
    } else {
        // The player plays the startup scene itself (Play Project)
        m_window.SetTitle(m_project->Name());
    }
    LOG_MESSAGE(log::Category::Core, LogSource, "Project %s opened from %s", m_project->Name().c_str(),
        m_project->File().string().c_str());
}

Path EngineApp::OpenStartupScene()
{
    if (!m_project->Desc().entry.startupScene)
        return { };
    // A broken scene file is logged; the editor opens with an empty scene
    auto scene = LoadStartupScene();
    if (!scene)
        return { };
    const auto handlers = m_models.InsertAll(std::move(scene->models), scene->modelFiles);
    (void)m_scene.Append(std::move(*scene), handlers);
    return m_project->Resolve(*m_project->Desc().entry.startupScene);
}

auto EngineApp::LoadStartupScene() const -> EngineResult<SceneAsset>
{
    const project::AssetPath& startupScene = *m_project->Desc().entry.startupScene;
    return resmgr::Serializer::LoadScene(m_project->Resolve(startupScene), m_project->Assets());
}

std::optional<script::PlayStart> EngineApp::MakePlayStart(script::PlayTarget target) const
{
    if (!m_project)
        return std::nullopt;
    script::PlayStart start { .mainModule = m_project->Desc().entry.mainModule };
    if (target == script::PlayTarget::Scene || !m_project->Desc().entry.startupScene)
        return start;
    auto scene = LoadStartupScene();
    if (!scene) {
        ERROR_MESSAGE(log::Category::Core, LogSource, "Play Project did not start: %s", scene.error().message.c_str());
        return std::nullopt;
    }
    start.scene = std::move(*scene);
    return start;
}

void EngineApp::BuildGame()
{
    if (!m_project)
        return;
    // Errors are logged: the editor goes on
    const auto executable = platform::CurrentExecutable();
    if (!executable)
        return;
    const auto folder = resmgr::ProjectBuilder::Build(*m_project, { *executable, FileSystem::Resolve("shaders") });
    if (folder)
        LOG_MESSAGE(log::Category::Core, LogSource, "Build Game: %s is in %s", m_project->Name().c_str(), folder->string().c_str());
}

void EngineApp::BuildOverlay(render::FrameWriter& frame)
{
    // UI logic (GLFW backend, widgets, platform windows) belongs to the main thread
    ELM_PROFILE_SCOPE_N("Build Overlay");
    if (m_editor) {
        m_editor->BuildFrame({ *m_renderSystem, m_scene, m_models, m_camera, m_currentStats,
            m_engineViewPort, frame, m_debugDraw.GetDebugDraw() });
        return;
    }
    const Size framebuffer = m_window.GetFramebufferSize();
    graphics::FitGameView(framebuffer, m_engineViewPort, m_camera);
    graphics::WriteGameOverlay(m_engineViewPort, framebuffer, frame.Overlay());
}

void EngineApp::SubmitFrame(render::FrameWriter& frame)
{
    ELM_PROFILE_SCOPE_N("Build Frame Snapshot");

    frame.SetBackbufferSize(m_window.GetFramebufferSize());

    const render::CameraData camera { m_camera.GetViewProjectionMatrix(), m_camera.GetPosition() };
    frame.BeginSceneView(camera, m_engineViewPort.GetColorTexture(), m_engineViewPort.GetSize());

    // The UI may have moved nodes since the update
    m_scene.UpdateWorldTransforms();
    if (const auto* drawn = m_scene.FindComponents<ModelComponent>())
        SubmitModels(*drawn, frame);

    m_debugDraw.EndFrame(frame);

    m_renderSystem->SubmitFrame(frame);
}

void EngineApp::SubmitModels(const ComponentStore<ModelComponent>& drawn, render::FrameWriter& frame)
{
    const auto ids = drawn.Ids();
    const auto components = drawn.Components();
    const auto worldTransforms = m_scene.WorldTransforms();
    size_t drawCount = 0;
    for (const auto& component : components)
        drawCount += m_models.GetMeshes(component.model).size();
    frame.ReserveDraws(drawCount);
    for (size_t index = 0; index < components.size(); ++index) {
        const ModelComponent& component = components[index];
        const size_t nodeIndex = m_scene.IndexOf(ids[index]);
        if (!component.visible || nodeIndex == Scene::NoNode)
            continue;
        // A node culling did not see yet (added by the UI, a scene just loaded) is drawn
        const auto* occludee = m_cullingSystem.FindOccludee(ids[index]);
        if (occludee && !occludee->isVisible)
            continue;
        const Vector4& tint = occludee ? occludee->color : component.color;
        m_models.ForEachSubmesh(component.model, [&](const graphics::Model::Submesh& submesh, render::MeshHandle mesh) {
            const Vector4 color {
                submesh.color.x * tint.x,
                submesh.color.y * tint.y,
                submesh.color.z * tint.z,
                submesh.color.w * tint.w
            };
            frame.Draw(mesh, worldTransforms[nodeIndex], color);
        });
    }
}

void EngineApp::FixedUpdate(float fixedDeltaTime)
{
    ELM_PROFILE_SCOPE_N("FixedUpdate Physics Step");
    // Edit mode: the scene stands still
    std::span<const CollisionEvent> collisions;
    if (m_physicsSystem && m_playMode.IsPlaying()) {
        m_physicsSystem->Step(m_scene, fixedDeltaTime);
        collisions = m_physicsSystem->CollisionEvents();
    }
    m_scriptSystem->FixedUpdate(fixedDeltaTime, collisions);
}

void EngineApp::Update(float deltaTime)
{
    ELM_PROFILE_SCOPE_N("Update Engine Logic");

    // Accumulate FPS statistics
    m_frameCounterTime += deltaTime;
    m_frameCount++;

    if (m_frameCounterTime >= 1.0f) {
        m_currentStats.fps = static_cast<float>(m_frameCount) / m_frameCounterTime;
        m_currentStats.deltaTimeMs = (m_frameCounterTime / static_cast<float>(m_frameCount)) * 1000.0f;
        m_frameCount = 0;
        m_frameCounterTime = 0.0f;
    }

    // The viewport state comes from the previous UI frame; the player has no editor camera
    m_cameraController.SetViewportState(m_editor && m_editor->IsSceneViewportHovered(), m_editor && m_editor->IsSceneViewportFocused());

    m_inputSystem->Update();

    // Before Play/Stop: a project switch stops Play itself
    ApplyProjectRequest();
    // Play/Stop asked for by scripts and windows since the last update
    m_playMode.ApplyRequest();
    // In Play the camera is the game's (elm.camera); the editor's fly camera waits for Stop
    m_cameraController.SetEnabled(!m_playMode.IsPlaying());
    m_scriptSystem->Update(deltaTime);
    // Scripts move nodes through local transforms
    m_scene.UpdateWorldTransforms();

    m_cameraController.Update(deltaTime);

    const Matrix4x4 cullingVP = m_camera.GetCullingViewProjection();
    m_cullingSystem.ExecuteCulling(m_scene, m_models, cullingVP, m_settings);

    if (m_physicsSystem) {
        m_currentStats.physicsBodyCount = m_physicsSystem->GetNumBodies();
    }
}

void EngineApp::Shutdown()
{
    if (!m_isRunning)
        return;

    std::cout << "[EngineApp] Shutting down systems..." << std::endl;

    // The settings are the editor's: the player only reads them
    if (m_editor) {
        if (auto savedSettings = m_settings.Save(Settings::DefaultFilePath()); !savedSettings)
            ERROR_MESSAGE(log::Category::Core, "EngineApp", "%s", savedSettings.error().message.c_str());
    }

    // Ensure the render thread is stopped before destroying resources
    m_renderSystem->StopRendering();
    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }

    // Scripts go first: they may still talk to the other systems in on_shutdown
    m_scriptSystem->Shutdown();

    if (m_physicsSystem) {
        m_physicsSystem->Shutdown();
    }

    if (m_editor) {
        m_editor->Shutdown();
    }
    m_scene.Clear();
    m_models.Clear();
    m_cullingSystem.Shutdown();
    m_engineViewPort = {};
    if (m_renderSystem) {
        m_renderSystem->Shutdown();
    }
    m_window.Destroy();
    core::JobSystem::Get().Shutdown();

    m_isRunning = false;
    std::cout << "[EngineApp] Engine shutdown finished." << std::endl;
}

} // namespace Engine
