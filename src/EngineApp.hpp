#pragma once

#include "core/Error.hpp"

#include "graphics/RenderSystem.hpp"
#include "graphics/ImGuiSystem.hpp"
#include "graphics/CameraController.hpp"
#include "graphics/Settings.hpp"
#include "graphics/culling/OcclusionCullingSystem.hpp"
#include "graphics/render/FramePacket.hpp"

#include "physics/PhysicsSystem.hpp"

#include "input/Input.hpp"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

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

		/// Render thread entry point — runs BeginFrame/Draw/ImGui/EndFrame loop.
		void RenderThreadFunc();

	private:
		UniquePtr<RenderSystem> m_renderSystem;
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

		// ── Pipelined rendering synchronization ──────────────────────────
		//
		// Double-buffered FramePacket: the update thread writes into one
		// slot while the render thread reads from the other.
		//
		// Protocol:
		//   1. Update thread fills m_framePackets[m_packetWriteIndex],
		//      then flips m_packetWriteIndex, sets m_frameReady=true, notifies.
		//   2. Render thread waits on m_frameReady, picks up the packet at
		//      the *previous* write index, renders it, sets m_renderDone=true.
		//   3. Update thread waits on m_renderDone before overwriting the
		//      same buffer again (back-pressure so we don't get >1 frame ahead).

		static constexpr size_t kPacketCount = 2;
		FramePacket  m_framePackets[kPacketCount];
		size_t       m_packetWriteIndex { 0 };   // toggled by update thread

		std::mutex              m_frameMutex;
		std::condition_variable m_frameCv;
		bool                    m_frameReady { false };
		bool                    m_renderDone { true };
		std::atomic<bool>       m_shouldExit { false };

		std::thread             m_renderThread;
	};

} // namespace Engine
