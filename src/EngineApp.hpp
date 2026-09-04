#pragma once

#include "core/Error.hpp"
#include "graphics/RenderSystem.hpp"
#include "graphics/ImGuiSystem.hpp"
#include "physics/PhysicsSystem.hpp"
#include "input/Input.hpp"

#include <memory>


namespace Engine {

	using namespace elm;

	class EngineApp {
	public:
		EngineApp();
		~EngineApp();

		EngineApp(const EngineApp&) = delete;
		EngineApp& operator=(const EngineApp&) = delete;
		EngineApp(EngineApp&&) noexcept = delete;
		EngineApp& operator=(EngineApp&&) noexcept = delete;

		[[nodiscard]] auto Init(uint32_t width = 1280, uint32_t height = 720, std::string_view title = "C++23 3D Engine Core") -> EngineResult<void>;

		[[nodiscard]] auto Run() -> EngineResult<void>;
		void Shutdown();

	private:
		void FixedUpdate(float fixedDeltaTime);
		void Update(float deltaTime);
		void Render(float deltaTime);

	private:
		std::unique_ptr<RenderSystem> m_renderSystem;
		std::unique_ptr<ImGuiSystem> m_imguiSystem;
		std::unique_ptr<PhysicsSystem> m_physicsSystem;
		std::unique_ptr<InputSystem> m_inputSystem;

			bool m_isRunning{ false };
		float m_fixedTimeStep{ 1.0f / 60.0f }; // 60 Hz physics step

		// FPS counter statistics
		float m_frameCounterTime{ 0.0f };
		uint32_t m_frameCount{ 0 };
		FrameStats m_currentStats;
	};

} // namespace Engine
