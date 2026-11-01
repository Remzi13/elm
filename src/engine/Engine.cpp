#include "engine/Engine.hpp"

#include "core/Timer.hpp"
#include "core/Profiling.hpp"
#include "core/Log.hpp"
#include "core/Unexpected.hpp"

#include "core/JobSystem.hpp"
#include "core/Threading.hpp"

#include "Scene/ModelComponent.hpp"

#include "resmgr/Serializer.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

namespace elm {

namespace {

    constexpr const char* LogSource = "Engine";
    /// Longer frames are cut to this: physics does not spiral into ever more steps.
    constexpr float MaxFrameSeconds = 0.25f;

} // namespace

Engine::Engine()
    : m_renderSystem(MakeUnique<render::RenderSystem>())
    , m_physicsSystem(MakeUnique<PhysicsSystem>())
    , m_inputSystem(MakeUnique<InputSystem>())
    , m_scriptSystem(MakeUnique<script::ScriptSystem>())
    , m_models(m_renderSystem->Resources())
    , m_playMode(m_scene, &m_models, m_physicsSystem.get(), *m_scriptSystem)
{
}

Engine::~Engine()
{
    Shutdown();
}

auto Engine::Init(const EngineDesc& desc) -> EngineResult<void>
{
    LOG_MESSAGE( log::Category::Core, LogSource, "Initializing 3D Engine Core (C++23)..." );

    m_scriptDebugDraw = desc.debugDraw;
    FileSystem::RegisterSettings(m_settings);
    log::registerSettings(m_settings);
    m_cullingSystem.RegisterSettings(m_settings);
    script::ScriptSystem::RegisterSettings(m_settings);
    script::PlayMode::RegisterSettings(m_settings);
    const Path settingsPath = Settings::DefaultFilePath();
    if (auto loadedSettings = m_settings.Load(settingsPath); !loadedSettings)
        return MakeUnexpected(loadedSettings.error());
    FileSystem::ApplySettings(m_settings, settingsPath.Parent());
    log::applySettings(m_settings, settingsPath.Parent());

    m_camera.SetAspect(static_cast<float>(desc.width) / static_cast<float>(desc.height));
    // Initialize the render backend and its command queue first.
    core::registerThread(core::ThreadRole::Update);
    ELM_PROFILE_THREAD("Main Thread");
    core::JobSystem::Get().Init();
    if (auto windowInit = m_window.Create(Size(desc.width, desc.height), desc.title); !windowInit) {
        return MakeUnexpected(windowInit.error());
    }
    auto renderInit = m_renderSystem->Init(m_window.GetNativeWindow(), m_window.GetFramebufferSize());
    if (!renderInit) {
        m_window.Destroy();
        return MakeUnexpected(renderInit.error());
    }

    m_engineViewPort = render::ViewPort(m_renderSystem->Resources(), Size(desc.width, desc.height));
    m_inputSystem->AttachWindow(m_window.GetHandle());

    // Default axes for the scripts (elm.input.axis): WASD moves, E and Q go up and down
    m_inputSystem->AddAxisMapping("MoveForward", Key::W, 1.0f);
    m_inputSystem->AddAxisMapping("MoveForward", Key::S, -1.0f);
    m_inputSystem->AddAxisMapping("MoveRight", Key::D, 1.0f);
    m_inputSystem->AddAxisMapping("MoveRight", Key::A, -1.0f);
    m_inputSystem->AddAxisMapping("MoveUp", Key::E, 1.0f);
    m_inputSystem->AddAxisMapping("MoveUp", Key::Q, -1.0f);

    // Initialize Physics System
    auto physicsInit = m_physicsSystem->Init();
    if (!physicsInit) {
        m_engineViewPort = {};
        m_renderSystem->Shutdown();
        return MakeUnexpected(physicsInit.error());
    }

    m_cullingSystem.Init(m_renderSystem->Resources(), m_settings);

    // The scripts start with the project (OpenProject), the main module with Play
    m_playMode.SetStartSource([this](script::PlayTarget target) { return MakePlayStart(target); });

    m_isRunning = true;
    m_physicsAccumulator = 0.0f;
    m_lastFrameTime = core::getTimeStamp();
    m_renderThread = std::thread(&Engine::RenderThreadFunc, this);

    LOG_MESSAGE( log::Category::Core, LogSource, "Engine initialization completed successfully." );
    
    return { };
}


// ─────────────────────────────────────────────────────────────────────────────
// Render thread — draws the frame snapshots submitted by the update (main) thread.
// ─────────────────────────────────────────────────────────────────────────────
void Engine::RenderThreadFunc()
{
    ELM_PROFILE_THREAD("Render Thread");
    core::registerThread(core::ThreadRole::Render);

    render::RenderedFrameInfo info;
    while (m_renderSystem->RenderNextFrame(info)) {
    }

    core::unregisterThread(core::ThreadRole::Render);
}

bool Engine::ShouldClose() const
{
    return m_window.ShouldClose();
}

float Engine::BeginFrame()
{
    ELM_PROFILE_FRAME();
    const uint64_t now = core::getTimeStamp();
    const float deltaTime = std::min(static_cast<float>(core::getMilliseconds(m_lastFrameTime, now)) / 1000.f, MaxFrameSeconds);
    m_lastFrameTime = now;
    m_physicsSystem->ClearFrameLog();
    {
        // GLFW events belong to the main thread
        ELM_PROFILE_SCOPE_N("Process Input");
        m_inputSystem->BeginFrame();
    }
    return deltaTime;
}

void Engine::Simulate(float deltaTime)
{
    m_physicsAccumulator += deltaTime;
    while (m_physicsAccumulator >= m_fixedTimeStep) {
        FixedUpdate(m_fixedTimeStep);
        m_physicsAccumulator -= m_fixedTimeStep;
    }
    Update(deltaTime);
}

render::FrameWriter Engine::BeginRenderFrame()
{
    return m_renderSystem->BeginFrame();
}

void Engine::StopRendering()
{
    m_renderSystem->StopRendering();
    if (m_renderThread.joinable())
        m_renderThread.join();
}

void Engine::LoadTestScene(ScenePreset preset, uint32_t instanceCount)
{
    TestScenes::BuildScene(preset, instanceCount, m_scene, m_models);
}

auto Engine::ReadProject(const Path& folderOrFile) -> EngineResult<project::Project>
{
    const Path file = project::Project::FindFile(FileSystem::Resolve(folderOrFile));
    auto loaded = resmgr::Serializer::LoadProject(file);
    if (loaded && StringView(loaded->Name()) != StringView(file.Stem().string()))
        WARNING_MESSAGE(log::Category::Core, LogSource, "Project \"%s\" is in %s: the name and the file should match",
            loaded->Name().c_str(), file.Filename().string().c_str());
    return loaded;
}

const project::Project* Engine::OpenedProject() const
{
    return m_project ? &*m_project : nullptr;
}

void Engine::SetProjectEntry(project::ProjectEntry entry)
{
    ELM_ASSERT(m_project && "the entry belongs to the open project");
    m_project->SetEntry(std::move(entry));
}

void Engine::CloseProject()
{
    if (!m_project)
        return;
    if (m_playMode.IsPlaying())
        m_playMode.Stop();
    // on_shutdown still sees the scene
    m_scriptSystem->Shutdown();
    m_physicsSystem->Clear();
    m_scene.Clear();
    m_models.Clear();
    LOG_MESSAGE(log::Category::Core, LogSource, "Project %s closed", m_project->Name().c_str());
    m_project.reset();
}

void Engine::OpenProject(project::Project project)
{
    ELM_ASSERT(!m_project && "the open project is closed first (CloseProject)");
    m_project = std::move(project);

    const script::EngineContext scriptEngine { &m_scene, &m_models, m_inputSystem.get(), &m_camera, &m_settings, m_physicsSystem.get(),
        m_playMode.Requests(), m_scriptDebugDraw, &*m_project };
    // In Edit the main module waits for Play: the Lua state serves the script props of Scene Hierarchy and the console
    if (auto scriptInit = m_scriptSystem->Init(scriptEngine, m_project->Scripts()); !scriptInit)
        ERROR_MESSAGE(log::Category::Script, LogSource, "%s", scriptInit.error().message.c_str());
    LOG_MESSAGE(log::Category::Core, LogSource, "Project %s opened from %s", m_project->Name().c_str(),
        m_project->File().string().c_str());
}

auto Engine::LoadStartupScene() const -> EngineResult<SceneAsset>
{
    const project::AssetPath& startupScene = *m_project->Desc().entry.startupScene;
    return resmgr::Serializer::LoadScene(m_project->Resolve(startupScene), m_project->Assets());
}

std::optional<script::PlayStart> Engine::MakePlayStart(script::PlayTarget target) const
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

void Engine::DrawScene(render::FrameWriter& frame)
{
    ELM_PROFILE_SCOPE_N("Build Frame Snapshot");

    frame.SetBackbufferSize(m_window.GetFramebufferSize());

    const render::CameraData camera { m_camera.GetViewProjectionMatrix(), m_camera.GetPosition() };
    frame.BeginSceneView(camera, m_engineViewPort.GetColorTexture(), m_engineViewPort.GetSize());

    // The UI may have moved nodes since the update
    m_scene.UpdateWorldTransforms();
    if (const auto* drawn = m_scene.FindComponents<ModelComponent>())
        SubmitModels(*drawn, frame);

}

void Engine::SubmitFrame(render::FrameWriter& frame)
{
    m_renderSystem->SubmitFrame(frame);
    log::flushFile();
}

void Engine::SubmitModels(const ComponentStore<ModelComponent>& drawn, render::FrameWriter& frame)
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

void Engine::FixedUpdate(float fixedDeltaTime)
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

void Engine::Update(float deltaTime)
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

    m_inputSystem->Update();

    m_scriptSystem->Update(deltaTime);
    // Scripts move nodes through local transforms
    m_scene.UpdateWorldTransforms();

    const Matrix4x4 cullingVP = m_camera.GetCullingViewProjection();
    m_cullingSystem.ExecuteCulling(m_scene, m_models, cullingVP, m_settings);

    if (m_physicsSystem) {
        m_currentStats.physicsBodyCount = m_physicsSystem->GetNumBodies();
    }
}

void Engine::Shutdown()
{
    if (!m_isRunning)
        return;

    std::cout << "[Engine] Shutting down systems..." << std::endl;

    // Ensure the render thread is stopped before destroying resources
    StopRendering();

    // Scripts go first: they may still talk to the other systems in on_shutdown
    m_scriptSystem->Shutdown();

    if (m_physicsSystem) {
        m_physicsSystem->Shutdown();
    }

    m_project.reset();
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
    std::cout << "[Engine] Engine shutdown finished." << std::endl;
}

} // namespace elm
