#include "EngineApp.hpp"
#include "graphics/ui/DepthPreviewWindow.hpp"
#include "graphics/render/TextureManager.hpp"

#include "core/Timer.hpp"
#include "core/Profiling.hpp"

#include <iostream>

namespace elm {

EngineApp::EngineApp()
    : m_renderSystem(MakeUnique<RenderSystem>())
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
    std::cout << "[EngineApp] Initializing 3D Engine Core (C++23)..." << std::endl;

    m_camera.SetAspect(static_cast<float>(width) / static_cast<float>(height));

    // Initialize the render backend and its command queue first.
    auto renderInit = m_renderSystem->Init(width, height, title);
    if (!renderInit) {
        return std::unexpected(renderInit.error());
    }

    auto& textureManager = render::TextureManager::Get();
    if (!textureManager.Init(&m_renderSystem->GetCommandQueue())) {
        m_renderSystem->Shutdown();
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to initialize Texture Manager"));
    }

    render::TextureInfo viewportTextureInfo;
    viewportTextureInfo.name = "Engine Viewport Color";
    viewportTextureInfo.width = width;
    viewportTextureInfo.height = height;
    viewportTextureInfo.format = render::TextureFormat::RGBA8_UNORM_SRGB;
    viewportTextureInfo.bindFlags = render::TextureBindFlags::BindRenderTarget | render::TextureBindFlags::BindShaderResource;
    render::TextureInfo viewportDepthInfo;
    viewportDepthInfo.name = "Engine Viewport Depth";
    viewportDepthInfo.width = width;
    viewportDepthInfo.height = height;
    viewportDepthInfo.format = render::TextureFormat::D32_FLOAT;
    viewportDepthInfo.bindFlags = render::TextureBindFlags::BindDepthStencil;
    m_renderSystem->InitializeEngineViewportTexture(viewportTextureInfo, viewportDepthInfo, width, height);
    m_inputSystem->AttachWindow(m_renderSystem->GetWindowHandle());
    m_inputSystem->AddSubscriber(&m_cameraController, static_cast<int32_t>(InputPriority::Gameplay), "CameraController");

    auto imguiInit = m_imguiSystem->Init(*m_renderSystem, m_settings, "Engine Debug UI");
    if (!imguiInit) {
        m_renderSystem->Shutdown();
        textureManager.Shutdown();
        return std::unexpected(imguiInit.error());
    }

    // Register UI input consumer for keyboard focus (e.g. typing in text fields)
    m_inputSystem->AddListener([](InputEvent& event) -> bool {
        ImGuiIO& io = ImGui::GetIO();
        if (event.type == InputEventType::Key || event.type == InputEventType::Character) {
            if (io.WantCaptureKeyboard) {
                return true;
            }
        }
        return false;
    },
        static_cast<int32_t>(InputPriority::UI), "ImGuiKeyboardFilter");

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
        m_renderSystem->Shutdown();
        textureManager.Shutdown();
        return std::unexpected(physicsInit.error());
    }

    m_cullingSystem.Init();
    if (auto* depthWindow = m_imguiSystem->GetWindow<DepthPreviewWindow>()) {
        depthWindow->SetCullingSystem(&m_cullingSystem);
    }

    m_isRunning = true;
    std::cout << "[EngineApp] Engine initialization completed successfully." << std::endl;
    return { };
}


// ─────────────────────────────────────────────────────────────────────────────
// Render thread — runs on a dedicated std::thread.
// Consumes FramePackets produced by the update (main) thread.
// ─────────────────────────────────────────────────────────────────────────────
void EngineApp::RenderThreadFunc()
{
    ELM_PROFILE_THREAD("Render Thread");

    while (true) {
        size_t readIndex;
        {
            ELM_PROFILE_SCOPE_N("Wait For Frame Packet");
            std::unique_lock lock(m_frameMutex);
            m_frameCv.wait(lock, [this] { return m_frameReady || m_shouldExit.load(std::memory_order_relaxed); });

            if (m_shouldExit.load(std::memory_order_relaxed) && !m_frameReady)
                break;

            readIndex = (m_packetWriteIndex + kPacketCount - 1) % kPacketCount;
            m_frameReady = false;
        }

        // ── Render the frame ──────────────────────────────────────────
        {
            ELM_PROFILE_SCOPE_N("Render Thread Execute Frame");
            FramePacket& packet = m_framePackets[readIndex];

            if (m_renderSystem) {
                m_renderSystem->BeginFrame();

                {
                    ELM_PROFILE_SCOPE_N("Draw Scene Objects");
                    m_renderSystem->Draw(packet.frameData);
                }

                {
                    // Only draws the UI captured on the main thread: ImGui/GLFW logic must not run here
                    ELM_PROFILE_SCOPE_N("Render ImGui UI");
                    m_imguiSystem->RenderFrame(*m_renderSystem, packet.ui);
                }

                m_renderSystem->EndFrame();
            }
        }

        // ── Signal the update thread that rendering is done ──────────
        {
            std::lock_guard lock(m_frameMutex);
            m_renderDone = true;
        }
        m_frameCv.notify_one();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop — runs on the main thread (required for GLFW event polling).
// ─────────────────────────────────────────────────────────────────────────────
auto EngineApp::Run() -> EngineResult<void>
{
    if (!m_isRunning) {
        return std::unexpected(EngineError(ErrorCode::UnknownError, "EngineApp::Run called without prior successful initialization"));
    }

    std::cout << "[EngineApp] Entering main loop with pipelined Update/Render." << std::endl;
    ELM_PROFILE_THREAD("Main Thread");

    // Start the render thread
    m_renderThread = std::thread(&EngineApp::RenderThreadFunc, this);

    auto lastTime = core::getTimeStamp();
    float accumulator = 0.0f;

    while (m_isRunning && !m_renderSystem->ShouldClose()) {
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

        // UI logic (GLFW backend, widgets, platform windows) belongs to the main thread.
        // The write slot is not read by the render thread: it renders the other one.
        {
            ELM_PROFILE_SCOPE_N("Build ImGui Frame");
            m_imguiSystem->BuildFrame(*m_renderSystem, m_scene, m_currentStats, m_framePackets[m_packetWriteIndex].ui);
        }

        m_settings.Flash();

        // ── 4. Wait for the render thread to finish the previous frame ─
        {
            ELM_PROFILE_SCOPE_N("Wait For Render Thread (Backpressure)");
            std::unique_lock lock(m_frameMutex);
            m_frameCv.wait(lock, [this] { return m_renderDone; });
            m_renderDone = false;
        }

        // ── 5. Build the FramePacket for this frame ───────────────────
        {
            ELM_PROFILE_SCOPE_N("Build FramePacket Snapshot");
            FramePacket& packet = m_framePackets[m_packetWriteIndex];

            packet.deltaTime = deltaTime;
            packet.stats = m_currentStats;

            // Camera snapshot
            m_camera.SetAspect(m_renderSystem->GetEngineViewportAspectRatio());
            packet.frameData.camera = m_camera;

            // Visible object list snapshot
            packet.frameData.objects.clear();
            for (const auto& inst : m_scene.instances) {
                if (inst.visible) {
                    packet.frameData.objects[inst.renderMesh].push_back({ inst.worldTransform, inst.color });
                }
            }

            // Flip the write index for next frame
            m_packetWriteIndex = (m_packetWriteIndex + 1) % kPacketCount;
        }

        // Commit queued render commands atomically to lock-free queue
        if (m_renderSystem) {
            m_renderSystem->CommitCommands();
        }

        // ── 6. Signal the render thread that a new frame is ready ─────
        {
            std::lock_guard lock(m_frameMutex);
            m_frameReady = true;
        }
        m_frameCv.notify_one();
    }

    // ── Signal render thread to exit ──────────────────────────────────────
    m_shouldExit.store(true, std::memory_order_release);
    m_frameCv.notify_one();
    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }

    std::cout << "[EngineApp] Main loop exited." << std::endl;
    return { };
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
    m_shouldExit.store(true, std::memory_order_release);
    m_frameCv.notify_one();
    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }

    if (m_physicsSystem) {
        m_physicsSystem->Shutdown();
    }

    // ImGui draw list copies are freed while the ImGui context is alive
    for (auto& packet : m_framePackets) {
        packet.ui.Clear();
    }

    if (m_imguiSystem) {
        m_imguiSystem->Shutdown();
    }
    if (m_renderSystem) {
        m_renderSystem->Shutdown();
    }
    render::TextureManager::Get().Shutdown();

    m_isRunning = false;
    std::cout << "[EngineApp] Engine shutdown finished." << std::endl;
}

} // namespace Engine
