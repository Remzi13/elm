#pragma once

#include "core/Error.hpp"

#include "platform/Window.hpp"

#include "render/RenderSystem.hpp"
#include "render/ViewPort.hpp"

#include "graphics/FrameStats.hpp"
#include "graphics/ImGuiSystem.hpp"
#include "graphics/CameraController.hpp"
#include "graphics/Settings.hpp"
#include "graphics/culling/OcclusionCullingSystem.hpp"

#include "physics/PhysicsSystem.hpp"

#include "input/Input.hpp"

#include <thread>

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

		void LoadTestScene(ScenePreset preset, uint32_t instanceCount = 1500);

	private:
		void FixedUpdate(float fixedDeltaTime);
		void Update(float deltaTime);
		void SubmitFrame(render::FrameWriter& frame);

		/// Render thread entry point: draws submitted frames until rendering is stopped.
		void RenderThreadFunc();

	private:
		platform::Window m_window;
		UniquePtr<render::RenderSystem> m_renderSystem;
		UniquePtr<ImGuiSystem> m_imguiSystem;
		UniquePtr<PhysicsSystem> m_physicsSystem;
		UniquePtr<InputSystem> m_inputSystem;

		Camera m_camera;
		CameraController m_cameraController;
		Scene m_scene;
		Settings m_settings;
		render::ViewPort m_engineViewPort;

		bool m_isRunning{ false };
		float m_fixedTimeStep{ 1.0f / 60.0f }; // 60 Hz physics step

		// FPS counter statistics
		float m_frameCounterTime{ 0.0f };
		uint32_t m_frameCount{ 0 };
		FrameStats m_currentStats;

		OcclusionCullingSystem m_cullingSystem;

		// Update and render run in parallel: the update thread builds frame N+1 while the render
		// thread draws frame N; RenderSystem's frame ring hands snapshots over (see render/frame/FrameRing.hpp).
		std::thread m_renderThread;
	};

} // namespace Engine
