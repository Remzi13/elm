#pragma once

#include "core/Error.hpp"
#include "graphics/RenderSystem.hpp"
#include "graphics/ImGuiSystem.hpp"
#include "physics/PhysicsSystem.hpp"
#include "input/Input.hpp"

namespace elm {

	class EngineApp {
	public:
		EngineApp();
		~EngineApp();

		EngineApp(const EngineApp&) = delete;
		EngineApp& operator=(const EngineApp&) = delete;
		EngineApp(EngineApp&&) noexcept = delete;
		EngineApp& operator=(EngineApp&&) noexcept = delete;

		[[nodiscard]] auto Init(uint32_t width = 1280, uint32_t height = 720, StringView title = "C++23 3D Engine Core") -> EngineResult<void>;

		[[nodiscard]] auto Run() -> EngineResult<void>;
		void Shutdown();

	private:
		void FixedUpdate(float fixedDeltaTime);
		void Update(float deltaTime);
		void Render(float deltaTime);

	private:
		UniquePtr<RenderSystem> m_renderSystem;
		UniquePtr<ImGuiSystem> m_imguiSystem;
		UniquePtr<PhysicsSystem> m_physicsSystem;
		UniquePtr<InputSystem> m_inputSystem;

		bool m_isRunning{ false };
		float m_fixedTimeStep{ 1.0f / 60.0f }; // 60 Hz physics step

		// FPS counter statistics
		float m_frameCounterTime{ 0.0f };
		uint32_t m_frameCount{ 0 };
		FrameStats m_currentStats;
	};

} // namespace Engine
