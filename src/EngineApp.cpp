#include "EngineApp.hpp"

#include "core/Timer.hpp"
#include "graphics/ui/SocLabWindow.hpp"

#include <algorithm>
#include <iostream>

namespace elm {

	EngineApp::EngineApp()
		: m_renderSystem(MakeUnique<RenderSystem>()),
		m_imguiSystem(MakeUnique<ImGuiSystem>()),
		m_physicsSystem(MakeUnique<PhysicsSystem>()),
		m_inputSystem(MakeUnique<InputSystem>()),
		m_cameraController(m_camera)
	{
	}

	EngineApp::~EngineApp() {
		Shutdown();
	}

	auto EngineApp::Init(uint32_t width, uint32_t height, StringView title) -> EngineResult<void> {
		std::cout << "[EngineApp] Initializing 3D Engine Core (C++23)..." << std::endl;


		m_camera.SetAspect(static_cast<float>(width) / static_cast<float>(height));

		// Initialize Render System
		auto renderInit = m_renderSystem->Init(width, height, title);
		if (!renderInit) {
			return std::unexpected(renderInit.error());
		}
		m_inputSystem->AttachWindow(m_renderSystem->GetWindowHandle());
		m_inputSystem->AddSubscriber(&m_cameraController, static_cast<int32_t>(InputPriority::Gameplay), "CameraController");

		ImGuiRenderContext imguiRenderContext;
		imguiRenderContext.renderSystem = m_renderSystem.get();
		imguiRenderContext.window = m_renderSystem->GetWindowHandle();
		imguiRenderContext.width = m_renderSystem->GetWidth();
		imguiRenderContext.height = m_renderSystem->GetHeight();
		auto imguiInit = m_imguiSystem->Init(imguiRenderContext, "Engine Debug UI");
		if (!imguiInit) {
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
			}, static_cast<int32_t>(InputPriority::UI), "ImGuiKeyboardFilter");

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
			return std::unexpected(physicsInit.error());
		}

		m_isRunning = true;
		std::cout << "[EngineApp] Engine initialization completed successfully." << std::endl;
		return {};
	}

	auto EngineApp::Run() -> EngineResult<void> {
		if (!m_isRunning) {
			return std::unexpected(EngineError(ErrorCode::UnknownError, "EngineApp::Run called without prior successful initialization"));
		}

		std::cout << "[EngineApp] Entering main loop with fixed timestep physics accumulator." << std::endl;

		auto lastTime = core::getTimeStamp();
		float accumulator = 0.0f;

		while (m_isRunning && !m_renderSystem->ShouldClose()) {
			auto currentTime = core::getTimeStamp();
			float deltaTime = static_cast<float>(core::getMilliseconds(lastTime, currentTime));
			lastTime = currentTime;

			// Cap maximum deltaTime to prevent physics spiral of death
			if (deltaTime > 0.25f) {
				deltaTime = 0.25f;
			}

			accumulator += deltaTime;

			// 
			{
				m_inputSystem->BeginFrame();
				// Fixed Timestep Physics Update
				while (accumulator >= m_fixedTimeStep) {
					FixedUpdate(m_fixedTimeStep);
					accumulator -= m_fixedTimeStep;
				}

				// Frame variable update & rendering
				Update(deltaTime);
			}
			// Draw
			{
				Render(deltaTime);
			}
		}

		std::cout << "[EngineApp] Main loop exited." << std::endl;
		return {};
	}

	void EngineApp::FixedUpdate(float fixedDeltaTime) {
		if (m_physicsSystem) {
			m_physicsSystem->Step(fixedDeltaTime);
		}
	}

	void EngineApp::Update(float deltaTime) {
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

		// Query synchronized physics transforms for display/rendering
		if (m_physicsSystem) {
			m_currentStats.physicsBodyCount = m_physicsSystem->GetNumBodies();
			m_currentStats.boxTransform = m_physicsSystem->GetDynamicBoxTransform();
			m_currentStats.groundTransform = m_physicsSystem->GetGroundTransform();
		}

		ImGuiRenderContext imguiRenderContext{
			m_renderSystem.get(),
			m_renderSystem->GetWindowHandle(),
			m_renderSystem->GetWidth(),
			m_renderSystem->GetHeight()
		};
		ImGuiUpdateContext imguiContext{
			m_scene,
			m_currentStats,
			m_renderSystem->GetWidth(),
			m_renderSystem->GetHeight(),
			m_renderSystem->GetMemAllocated(),
			m_renderSystem->GetEngineViewportTexture(),
			m_renderSystem->GetEngineViewportWidth(),
			m_renderSystem->GetEngineViewportHeight(),
			m_renderSystem->GetDepthPreviewTexture(),
			&m_renderSystem->GetCullingSystem().GetDepthBuffer(),
			m_renderSystem->GetDepthPreviewWidth(),
			m_renderSystem->GetDepthPreviewHeight(),
			m_renderSystem->IsDepthPreviewFalseColor(),
			m_renderSystem->GetCullingSystem().GetStats(),
			m_renderSystem->GetCullingSystem().enableFrustumCulling,
			m_renderSystem->GetCullingSystem().enableOcclusionCulling,
			m_renderSystem->GetCullingSystem().depthBias,
			m_renderSystem->GetCullingSystem().visualMode
		};
		auto updateConfig = m_imguiUpdateConfig;
		m_imguiSystem->Update(imguiContext, updateConfig);
		m_imguiUpdateConfig = updateConfig;
		{
			std::scoped_lock lock{ m_imguiConfigMutex };
			m_imguiRenderConfig = updateConfig;
		}
	}

	void EngineApp::Render([[maybe_unused]] float deltaTime) {
		if (!m_renderSystem) return;

		ImGuiConfig renderConfig;
		{
			std::scoped_lock lock{ m_imguiConfigMutex };
			renderConfig = m_imguiRenderConfig;
		}
		auto& culling = m_renderSystem->GetCullingSystem();
		culling.enableFrustumCulling = renderConfig.enableFrustumCulling;
		culling.enableOcclusionCulling = renderConfig.enableOcclusionCulling;
		culling.depthBias = renderConfig.depthBias;
		culling.visualMode = renderConfig.visualMode;
		m_renderSystem->SetDepthPreviewFalseColor(renderConfig.depthPreviewFalseColor);
		const auto resolutionIndex = std::clamp(renderConfig.resolution, 0, static_cast<int>(SocLabWindow::ResolutionWidths.size()) - 1);
		renderConfig.resolution = resolutionIndex;
		if (m_appliedResolution != resolutionIndex) {
			const auto width = SocLabWindow::ResolutionWidths[resolutionIndex];
			const auto height = SocLabWindow::ResolutionHeights[resolutionIndex];
			culling.SetResolution(width, height);
			m_renderSystem->CreateDepthPreviewTexture(width, height);
			m_appliedResolution = resolutionIndex;
		}

		m_renderSystem->BeginFrame();
		m_renderSystem->RenderScene(m_camera, m_scene);
		ImGuiRenderContext imguiRenderContext{
			m_renderSystem.get(),
			m_renderSystem->GetWindowHandle(),
			m_renderSystem->GetWidth(),
			m_renderSystem->GetHeight()
		};
		m_imguiSystem->Render();
		m_renderSystem->EndFrame();
	}

	void EngineApp::Shutdown() {
		if (!m_isRunning) return;

		std::cout << "[EngineApp] Shutting down systems..." << std::endl;

		if (m_physicsSystem) {
			m_physicsSystem->Shutdown();
		}

		if (m_imguiSystem) {
			m_imguiSystem->Shutdown();
		}
		if (m_renderSystem) {
			m_renderSystem->Shutdown();
		}

		m_isRunning = false;
		std::cout << "[EngineApp] Engine shutdown finished." << std::endl;
	}

} // namespace Engine
