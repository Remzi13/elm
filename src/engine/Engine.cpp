#include "engine/Engine.hpp"

#include "core/Timer.hpp"
#include "core/Profiling.hpp"
#include "core/Log.hpp"
#include "core/Unexpected.hpp"

#include "core/JobSystem.hpp"
#include "core/Threading.hpp"

#include "Scene/ModelComponent.hpp"

#include "resource/Serializer.hpp"

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
    : m_physicsSystem(MakeUnique<PhysicsSystem>())
    , m_scriptSystem(MakeUnique<script::ScriptSystem>())
    , m_scene(m_resources.GetModels())
    , m_graphics(m_settings, m_scene, m_resources.GetModels())
    , m_playMode(m_scene, m_physicsSystem.get(), *m_scriptSystem)
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
    m_graphics.RegisterSettings();
    script::ScriptSystem::RegisterSettings(m_settings);
    script::PlayMode::RegisterSettings(m_settings);
    const Path settingsPath = Settings::DefaultFilePath();
    if (auto loadedSettings = m_settings.Load(settingsPath); !loadedSettings)
        return MakeUnexpected(loadedSettings.error());
    FileSystem::ApplySettings(m_settings, settingsPath.Parent());
    log::applySettings(m_settings, settingsPath.Parent());

    core::registerThread(core::ThreadRole::Update);
    ELM_PROFILE_THREAD("Main Thread");
    core::JobSystem::Get().Init();
    if (auto platformInit = m_platform.Init(Size(desc.width, desc.height), desc.title); !platformInit)
        return MakeUnexpected(platformInit.error());
    const platform::Window& window = m_platform.GetWindow();
    if (auto graphicsInit = m_graphics.Init(window.GetNativeWindow(), window.GetFramebufferSize(), Size(desc.width, desc.height));
        !graphicsInit) {
        m_platform.Shutdown();
        return MakeUnexpected(graphicsInit.error());
    }

    // Initialize Physics System
    auto physicsInit = m_physicsSystem->Init();
    if (!physicsInit) {
        m_graphics.Shutdown();
        return MakeUnexpected(physicsInit.error());
    }

    // The scripts start with the project (OpenProject), the main module with Play
    m_playMode.SetStartSource([this](script::PlayTarget target) { return MakePlayStart(target); });

    m_isRunning = true;
    m_physicsAccumulator = 0.0f;
    m_lastFrameTime = core::getTimeStamp();

    LOG_MESSAGE( log::Category::Core, LogSource, "Engine initialization completed successfully." );
    
    return { };
}


bool Engine::ShouldClose() const
{
    return m_platform.ShouldClose();
}

float Engine::BeginFrame()
{
    ELM_PROFILE_FRAME();
    const uint64_t now = core::getTimeStamp();
    const float deltaTime = std::min(static_cast<float>(core::getMilliseconds(m_lastFrameTime, now)) / 1000.f, MaxFrameSeconds);
    m_lastFrameTime = now;
    m_physicsSystem->ClearFrameLog();
    m_platform.PollEvents();
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
    return m_graphics.BeginFrame();
}

void Engine::StopRendering()
{
    m_graphics.StopRendering();
}

void Engine::LoadTestScene(ScenePreset preset, uint32_t instanceCount)
{
    TestScenes::BuildScene(preset, instanceCount, m_scene.EditScene(), m_resources.GetModels());
}

void Engine::CloseProject()
{
    if (!m_resources.GetProject())
        return;
    if (m_playMode.IsPlaying())
        m_playMode.Stop();
    // on_shutdown still sees the scene
    m_scriptSystem->Shutdown();
    m_physicsSystem->Clear();
    m_scene.Clear();
    m_resources.Close();
}

void Engine::OpenProject(project::Project project)
{
    m_resources.Open(std::move(project));
    const project::Project& opened = *m_resources.GetProject();

    const script::EngineContext scriptEngine { &m_scene.EditScene(), &m_resources.GetModels(), &m_platform.GetInput(), &m_graphics.GetViewCamera(), &m_settings,
        m_physicsSystem.get(), m_playMode.Requests(), m_scriptDebugDraw, &opened };
    // In Edit the main module waits for Play: the Lua state serves the script props of Scene Hierarchy and the console
    if (auto scriptInit = m_scriptSystem->Init(scriptEngine, opened.Scripts()); !scriptInit)
        ERROR_MESSAGE(log::Category::Script, LogSource, "%s", scriptInit.error().message.c_str());
}

std::optional<script::PlayStart> Engine::MakePlayStart(script::PlayTarget target) const
{
    const project::Project* project = m_resources.GetProject();
    if (!project)
        return std::nullopt;
    script::PlayStart start { .mainModule = project->Desc().entry.mainModule };
    if (target == script::PlayTarget::Scene || !project->Desc().entry.startupScene)
        return start;
    auto scene = m_resources.LoadStartupScene();
    if (!scene) {
        ERROR_MESSAGE(log::Category::Core, LogSource, "Play Project did not start: %s", scene.error().message.c_str());
        return std::nullopt;
    }
    start.scene = std::move(*scene);
    return start;
}

void Engine::DrawScene(render::FrameWriter& frame)
{
    // The UI may have moved nodes since the update; graphics only reads the scene
    m_scene.UpdateTransforms();
    m_graphics.DrawWorld(frame, m_platform.GetWindow().GetFramebufferSize());
}

void Engine::SubmitFrame(render::FrameWriter& frame)
{
    m_graphics.EndFrame(frame);
    log::flushFile();
}

void Engine::FixedUpdate(float fixedDeltaTime)
{
    ELM_PROFILE_SCOPE_N("FixedUpdate Physics Step");
    // Edit mode: the scene stands still
    std::span<const CollisionEvent> collisions;
    if (m_physicsSystem && m_playMode.IsPlaying()) {
        m_physicsSystem->Step(m_scene.EditScene(), fixedDeltaTime);
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

    m_platform.DispatchInput();

    m_scriptSystem->Update(deltaTime);
    // Scripts move nodes through local transforms
    m_scene.UpdateTransforms();

    m_graphics.Cull();

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

    m_scene.Clear();
    m_resources.Close();
    m_graphics.Shutdown();
    m_platform.Shutdown();
    core::JobSystem::Get().Shutdown();

    m_isRunning = false;
    std::cout << "[Engine] Engine shutdown finished." << std::endl;
}

} // namespace elm
