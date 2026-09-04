#include "EngineApp.hpp"

#include <chrono>
#include <iostream>

namespace elm  {

	EngineApp::EngineApp()
		: m_renderSystem(std::make_unique<RenderSystem>()),
		m_imguiSystem(std::make_unique<ImGuiSystem>()),
		m_physicsSystem(std::make_unique<PhysicsSystem>()),
		m_inputSystem(std::make_unique<InputSystem>())
	{
	}

	EngineApp::~EngineApp() {
		Shutdown();
	}

	auto EngineApp::Init(uint32_t width, uint32_t height, StringView title) -> EngineResult<void> {
		std::cout << "[EngineApp] Initializing 3D Engine Core (C++23)..." << std::endl;

		// Initialize Render System
		auto renderInit = m_renderSystem->Init(width, height, title);
		if (!renderInit) {
			return std::unexpected(renderInit.error());
		}
		m_inputSystem->AttachWindow(m_renderSystem->GetWindowHandle());

		auto imguiInit = m_imguiSystem->Init(*m_renderSystem, "Engine Debug UI");
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

		auto lastTime = std::chrono::high_resolution_clock::now();
		float accumulator = 0.0f;

		while (m_isRunning && !m_renderSystem->ShouldClose()) {
			auto currentTime = std::chrono::high_resolution_clock::now();
			float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
			lastTime = currentTime;

			// Cap maximum deltaTime to prevent physics spiral of death
			if (deltaTime > 0.25f) {
				deltaTime = 0.25f;
			}

			accumulator += deltaTime;

			m_inputSystem->BeginFrame();

			// Poll window events
			m_renderSystem->PollEvents();

			// Fixed Timestep Physics Update
			while (accumulator >= m_fixedTimeStep) {
				FixedUpdate(m_fixedTimeStep);
				accumulator -= m_fixedTimeStep;
			}

			// Frame variable update & rendering
			Update(deltaTime);
			Render(deltaTime);
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

		// Update camera and rendering inputs
		if (m_renderSystem) {
			m_renderSystem->Update(deltaTime, m_inputSystem->GetEvents());
		}

		// Query synchronized physics transforms for display/rendering
		if (m_physicsSystem) {
			m_currentStats.physicsBodyCount = m_physicsSystem->GetNumBodies();
			m_currentStats.boxTransform = m_physicsSystem->GetDynamicBoxTransform();
			m_currentStats.groundTransform = m_physicsSystem->GetGroundTransform();
		}
	}

	void EngineApp::Render([[maybe_unused]] float deltaTime) {
		if (!m_renderSystem) return;

		m_renderSystem->BeginFrame();
		m_renderSystem->RenderScene();
		m_imguiSystem->BeginFrame(*m_renderSystem);
		m_imguiSystem->Render(*m_renderSystem, m_currentStats);
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
