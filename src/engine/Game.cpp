#include "engine/Game.hpp"

#include "core/JobSystem.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"
#include "core/Threading.hpp"
#include "core/Unexpected.hpp"

#include "resource/ProgramSettings.hpp"

#include "script/ProjectPlayStart.hpp"

#if defined(ELM_DEBUG_DRAW)
#include "graphics/debug/ColliderVisualizer.hpp"
#include "graphics/debug/PhysicsVisualizer.hpp"
#endif

#include "project/Project.hpp"

#include <utility>

namespace elm {

    namespace {

        constexpr const char* LogSource = "Game";

        /// Debug drawing for elm.debug and the visualizers, in a build with ELM_DEBUG_DRAW.
        UniquePtr<graphics::DebugDrawing> MakeDebugDrawing([[maybe_unused]] Settings& settings)
        {
#if defined(ELM_DEBUG_DRAW)
            return MakeUnique<graphics::DebugDrawing>(settings);
#else
            return nullptr;
#endif
        }

    } // namespace

    Game::Game()
        : m_scene(m_resources.GetModels())
        , m_graphics(m_settings, m_scene, m_resources.GetModels())
        , m_debugDraw(MakeDebugDrawing(m_settings))
        , m_scripts({ .scene = &m_scene, .physics = &m_physics, .resources = &m_resources, .input = &m_platform.GetInput(), .camera = &m_graphics.GetViewCamera(), .settings = &m_settings, .debugDraw = m_debugDraw ? &m_debugDraw->GetDebugDraw() : nullptr })
    {
    }

    Game::~Game()
    {
        Shutdown();
    }

    auto Game::Init(const GameDesc& desc) -> EngineResult<void>
    {
        // A game does not watch its script files (the editor does)
        m_settings.Set(Settings::Category::Script, script::ScriptSystem::HotReloadSetting, false);
        script::ScriptSystem::RegisterSettings(m_settings);
        if (auto loaded = resource::LoadProgramSettings(m_settings); !loaded)
            return loaded;
        core::registerThread(core::ThreadRole::Update);
        ELM_PROFILE_THREAD("Main Thread");
        core::JobSystem::Get().Init();
        if (auto platformInit = m_platform.Init(desc.window, desc.title); !platformInit)
            return platformInit;
        const platform::Window& window = m_platform.GetWindow();
        if (auto graphicsInit = m_graphics.Init(window.GetNativeWindow(), window.GetFramebufferSize(), desc.window); !graphicsInit) {
            m_platform.Shutdown();
            return graphicsInit;
        }
        if (auto physicsInit = m_physics.Init(); !physicsInit) {
            m_graphics.Shutdown();
            m_platform.Shutdown();
            return physicsInit;
        }
#if defined(ELM_DEBUG_DRAW)
        AddVisualizers();
#endif
        m_scripts.GetPlayMode().SetStartSource([this](script::PlayTarget target) { return script::MakeProjectPlayStart(m_resources, target); });
        m_initialized = true;
        return {};
    }

#if defined(ELM_DEBUG_DRAW)
    void Game::AddVisualizers()
    {
        graphics::DebugDraw& debugDraw = m_debugDraw->GetDebugDraw();
        m_debugDraw->AddVisualizer(MakeUnique<graphics::ColliderVisualizer>(m_scene.GetScene(), debugDraw));
        m_debugDraw->AddVisualizer(MakeUnique<graphics::PhysicsVisualizer>(m_physics.FrameLog(), debugDraw));
    }
#endif

    auto Game::OpenProject(const Path& folderOrFile) -> EngineResult<void>
    {
        auto read = resource::ResourceSystem::ReadProject(folderOrFile);
        if (!read)
            return MakeUnexpected(read.error());
        m_platform.GetWindow().SetTitle(read->Name());
        m_resources.Open(std::move(*read));
        return m_scripts.Init(m_resources.GetProject()->Scripts());
    }

    void Game::LoadTestScene(ScenePreset preset, uint32_t instanceCount)
    {
        TestScenes::BuildScene(preset, instanceCount, m_scene.EditScene(), m_resources.GetModels());
    }

    void Game::Run()
    {
        m_scripts.GetPlayMode().RequestPlay(script::PlayTarget::Project);
        m_clock.Start();
        while (RunFrame()) {
        }
    }

    bool Game::RunFrame()
    {
        // No Edit mode to go back to: Stop (on_stop still runs) or a Play that could not start ends the game
        script::PlayMode& play = m_scripts.GetPlayMode();
        const bool stopRequested = play.GetRequest() == script::PlayMode::Request::Stop;
        play.ApplyRequest();
        if (stopRequested || !play.IsPlaying())
            return false;

        ELM_PROFILE_FRAME();
        const float deltaTime = m_clock.Tick();
        m_physics.ClearFrameLog();
        m_platform.PollEvents();
        // Before anything of the frame draws, physics steps included
        if (m_debugDraw)
            m_debugDraw->BeginFrame(deltaTime);
        Simulate(deltaTime);
        Draw();
        return !m_platform.ShouldClose();
    }

    void Game::Simulate(float deltaTime)
    {
        for (uint32_t step = m_clock.TakeFixedSteps(deltaTime); step > 0; --step) {
            ELM_PROFILE_SCOPE_N("FixedUpdate Physics Step");
            m_physics.Step(m_scene.EditScene(), FrameClock::FixedStep);
            m_scripts.FixedUpdate(FrameClock::FixedStep, m_physics.CollisionEvents());
        }
        m_platform.DispatchInput();
        m_scripts.Update(deltaTime);
        // Scripts move nodes through local transforms
        m_scene.UpdateTransforms();
        m_graphics.Cull();
    }

    void Game::Draw()
    {
        render::FrameWriter frame = m_graphics.BeginFrame();
        if (!frame)
            return;
        const Size framebuffer = m_platform.GetWindow().GetFramebufferSize();
        m_graphics.PresentToWindow(frame, framebuffer);
        m_graphics.DrawWorld(frame, framebuffer);
        if (m_debugDraw)
            m_debugDraw->EndFrame(frame);
        m_graphics.EndFrame(frame);
        log::flushFile();
    }

    void Game::Shutdown()
    {
        if (!m_initialized)
            return;
        LOG_MESSAGE(log::Category::Core, LogSource, "Shutting down systems...");
        // The render thread stops before the resources it may still use go
        m_graphics.StopRendering();
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

} // namespace elm
