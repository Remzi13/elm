#include "EngineApp.hpp"

#include "core/Timer.hpp"
#include "core/Profiling.hpp"
#include "core/Log.hpp"

#include "core/JobSystem.hpp"
#include "core/Threading.hpp"

#include "graphics/ui/DepthPreviewWindow.hpp"

#include <iostream>

namespace elm {

EngineApp::EngineApp()
    : m_renderSystem(MakeUnique<render::RenderSystem>())
    , m_imguiSystem(MakeUnique<ImGuiSystem>())
    , m_physicsSystem(MakeUnique<PhysicsSystem>())
    , m_inputSystem(MakeUnique<InputSystem>())
    , m_cameraController(m_camera)
{
}

EngineApp::~EngineApp()
{
    Shutdown();
}

auto EngineApp::Init(uint32_t width, uint32_t height, StringView title) -> EngineResult<void>
{ 
 
    LOG_MESSAGE( log::Category::Core, "EngineApp", "Initializing 3D Engine Core (C++23)..." );    

    m_camera.SetAspect(static_cast<float>(width) / static_cast<float>(height));
    // Initialize the render backend and its command queue first.
    core::registerThread(core::ThreadRole::Update);
    core::JobSystem::Get().Init();
    if (auto windowInit = m_window.Create(Size(width, height), title); !windowInit) {
        return std::unexpected(windowInit.error());
    }
    auto renderInit = m_renderSystem->Init(m_window.GetNativeWindow(), m_window.GetFramebufferSize());
    if (!renderInit) {
        m_window.Destroy();
        return std::unexpected(renderInit.error());
    }

    m_engineViewPort = render::ViewPort(m_renderSystem->Resources(), Size(width, height));
    m_inputSystem->AttachWindow(m_window.GetHandle());
    m_inputSystem->AddSubscriber(&m_cameraController, static_cast<int32_t>(InputPriority::Gameplay), "CameraController");

    auto imguiInit = m_imguiSystem->Init(m_window, *m_renderSystem, m_settings, "Engine Debug UI");
    if (!imguiInit) {
        m_engineViewPort = {};
        m_renderSystem->Shutdown();
        return std::unexpected(imguiInit.error());
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
        m_imguiSystem->Shutdown();
        m_engineViewPort = {};
        m_renderSystem->Shutdown();
        return std::unexpected(physicsInit.error());
    }

    m_cullingSystem.Init(m_renderSystem->Resources());
    if (auto* depthWindow = m_imguiSystem->GetWindow<DepthPreviewWindow>()) {
        depthWindow->SetCullingSystem(&m_cullingSystem);
    }

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
        m_imguiSystem->NotifyViewportSurfacesReleased(info.releasedSurfaceCount);
    }

    core::unregisterThread(core::ThreadRole::Render);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop — runs on the main thread (required for GLFW event polling).
// ─────────────────────────────────────────────────────────────────────────────
auto EngineApp::Run() -> EngineResult<void>
{
    if (!m_isRunning) {
        return std::unexpected(EngineError(ErrorCode::UnknownError, "EngineApp::Run called without prior successful initialization"));
    }

    LOG_MESSAGE( log::Category::Core, "EngineApp", "Entering main loop with pipelined Update/Render." );
    
    ELM_PROFILE_THREAD("Main Thread");

    // Start the render thread
    m_renderThread = std::thread(&EngineApp::RenderThreadFunc, this);

    auto lastTime = core::getTimeStamp();
    float accumulator = 0.0f;

    while (m_isRunning && !m_window.ShouldClose()) {
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
            const bool falseColor = m_settings.Get<bool>(Settings::Category::Render, CULLING_DEPTH_FALSE_COLOR);
            m_cullingSystem.UpdateDepthPreviewTexture(falseColor);
        }

        // ── 4. Build and submit the frame snapshot ────────────────────
        // Blocks only when the render thread is two frames behind
        auto frame = m_renderSystem->BeginFrame();
        if (!frame)
            break;

        // UI logic (GLFW backend, widgets, platform windows) belongs to the main thread
        {
            ELM_PROFILE_SCOPE_N("Build ImGui Frame");
            m_imguiSystem->BuildFrame(*m_renderSystem, m_scene, m_camera, m_currentStats,
                m_engineViewPort, frame.Overlay());
        }

        m_settings.Flash();

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
    TestScenes::BuildScene(preset, instanceCount, m_scene, m_renderSystem->Resources());
}

void EngineApp::SubmitFrame(render::FrameWriter& frame)
{
    ELM_PROFILE_SCOPE_N("Build Frame Snapshot");

    frame.SetBackbufferSize(m_window.GetFramebufferSize());

    const render::CameraData camera { m_camera.GetViewProjectionMatrix(), m_camera.GetPosition() };
    frame.SetSceneView(camera, m_engineViewPort.GetColorTexture(), m_engineViewPort.GetSize());

    frame.ReserveDraws(m_scene.instances.size());
    for (const auto& inst : m_scene.instances) {
        if (inst.visible)
            frame.Draw(inst.renderMesh, inst.worldTransform, inst.color);
    }

    m_renderSystem->SubmitFrame(frame);
}

void EngineApp::FixedUpdate(float fixedDeltaTime)
{
    ELM_PROFILE_SCOPE_N("FixedUpdate Physics Step");
    if (m_physicsSystem) {
        m_physicsSystem->Step(fixedDeltaTime);
    }
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

    m_inputSystem->Update();

    m_cameraController.Update(deltaTime);

    const Matrix4x4 cullingVP = m_camera.GetCullingViewProjection();
    Vector<OccludeeInstance> occludees;
    m_cullingSystem.ExecuteCulling(m_scene, cullingVP, occludees);

    // Query synchronized physics transforms for display/rendering
    if (m_physicsSystem) {
        m_currentStats.physicsBodyCount = m_physicsSystem->GetNumBodies();
        m_currentStats.boxTransform = m_physicsSystem->GetDynamicBoxTransform();
        m_currentStats.groundTransform = m_physicsSystem->GetGroundTransform();
    }
}


void EngineApp::Shutdown()
{
    if (!m_isRunning)
        return;

    std::cout << "[EngineApp] Shutting down systems..." << std::endl;

    // Ensure the render thread is stopped before destroying resources
    m_renderSystem->StopRendering();
    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }

    if (m_physicsSystem) {
        m_physicsSystem->Shutdown();
    }

    if (m_imguiSystem) {
        m_imguiSystem->Shutdown();
    }
    m_scene.Clear();
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
