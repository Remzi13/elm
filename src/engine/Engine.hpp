#pragma once

#include "core/Error.hpp"
#include "core/Settings.hpp"

#include "platform/Window.hpp"

#include "render/RenderSystem.hpp"
#include "render/ViewPort.hpp"

#include "graphics/FrameStats.hpp"
#include "graphics/Camera.hpp"
#include "graphics/culling/OcclusionCullingSystem.hpp"
#include "graphics/ModelManager.hpp"

#include "Scene/TestScenes.hpp"

#include "physics/PhysicsSystem.hpp"

#include "project/Project.hpp"

#include "input/Input.hpp"

#include "script/PlayMode.hpp"
#include "script/ScriptSystem.hpp"

#include <optional>
#include <thread>

namespace elm {

	struct ModelComponent;

	namespace graphics {
		class DebugDraw;
	}

	/// What the engine is started with.
	struct EngineDesc {
		uint32_t width { 1280 };
		uint32_t height { 720 };
		StringView title { "Elm" };
		/// What the scripts' elm.debug draws into, owned by the program for as long as the engine runs.
		/// Null: elm.debug draws nothing (a game built without ELM_DEBUG_DRAW).
		graphics::DebugDraw* debugDraw { nullptr };
	};

	/// The engine: the systems of a running game (window and rendering, physics, input, scripts, scene,
	/// models, culling, Play) and the open project. It has no loop of its own and calls nobody back: the
	/// program (elm, elm_editor) runs the frame with its steps and adds what it needs between them.
	///
	///     const float deltaTime = engine.BeginFrame();
	///     engine.Simulate(deltaTime);
	///     render::FrameWriter frame = engine.BeginRenderFrame();   // the program writes its overlay
	///     engine.DrawScene(frame);                                 // then its debug primitives
	///     engine.SubmitFrame(frame);
	///
	/// After Shutdown it can be initialized again. Owner: the program. Thread: update (the main thread);
	/// the render thread is the engine's own, between Init and StopRendering.
	class Engine {
	public:
		Engine();
		~Engine();

		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;
		Engine(Engine&&) noexcept = delete;
		Engine& operator=(Engine&&) noexcept = delete;

		/// Reads .settings (the program registers its own settings in GetSettings() before), opens the window,
		/// starts rendering and its thread, physics and culling.
		[[nodiscard]] auto Init(const EngineDesc& desc) -> EngineResult<void>;
		/// Stops the render thread; the program then frees what it rendered with (UI) before Shutdown.
		void StopRendering();
		/// Ends the scripts and every system. Calls StopRendering if the program did not.
		void Shutdown();
		[[nodiscard]] bool IsRunning() const noexcept { return m_isRunning; }
		/// The window was asked to close.
		[[nodiscard]] bool ShouldClose() const;

		// --- The frame, in this order ---

		/// The frame's time step (capped against the physics spiral of death), the input of the window,
		/// the start of the physics frame log.
		[[nodiscard]] float BeginFrame();
		/// The fixed physics steps, the scripts and culling from the camera as it is now. Play/Stop requests
		/// wait for the program (PlayMode::GetRequest, ApplyRequest), which also moves its own camera before.
		void Simulate(float deltaTime);
		/// Blocks only while the render thread is two frames behind; invalid once rendering is stopped.
		[[nodiscard]] render::FrameWriter BeginRenderFrame();
		/// The engine view: the shown models culling did not reject. It stays the current scene view of
		/// frame, so the program's debug primitives go into it.
		void DrawScene(render::FrameWriter& frame);
		/// Hands the frame to the render thread.
		void SubmitFrame(render::FrameWriter& frame);

		// --- The project ---

		/// The project of a project folder or its .elmproject file; a relative path starts at the engine root.
		[[nodiscard]] static auto ReadProject(const Path& folderOrFile) -> EngineResult<project::Project>;
		/// Makes the project the open one: its scripts start. Precondition: no project is open (CloseProject).
		void OpenProject(project::Project project);
		/// Stops Play, ends the scripts, clears the scene, models and bodies. Nothing when no project is open.
		void CloseProject();
		/// The open project; null until one is opened.
		[[nodiscard]] const project::Project* OpenedProject() const;
		/// The open project's entry in memory, what the next Play starts with; the file is the caller's.
		void SetProjectEntry(project::ProjectEntry entry);
		/// The startup scene of the open project from its file. Precondition: the entry has a startup scene.
		[[nodiscard]] auto LoadStartupScene() const -> EngineResult<SceneAsset>;
		void LoadTestScene(ScenePreset preset, uint32_t instanceCount = 1500);

		[[nodiscard]] Settings& GetSettings() noexcept { return m_settings; }
		[[nodiscard]] platform::Window& GetWindow() noexcept { return m_window; }
		[[nodiscard]] render::RenderSystem& GetRenderSystem() noexcept { return *m_renderSystem; }
		[[nodiscard]] render::ViewPort& GetViewPort() noexcept { return m_engineViewPort; }
		[[nodiscard]] Scene& GetScene() noexcept { return m_scene; }
		[[nodiscard]] graphics::ModelManager& GetModels() noexcept { return m_models; }
		[[nodiscard]] Camera& GetCamera() noexcept { return m_camera; }
		[[nodiscard]] OcclusionCullingSystem& GetCulling() noexcept { return m_cullingSystem; }
		[[nodiscard]] InputSystem& GetInput() noexcept { return *m_inputSystem; }
		[[nodiscard]] PhysicsSystem& GetPhysics() noexcept { return *m_physicsSystem; }
		[[nodiscard]] script::ScriptSystem& GetScripts() noexcept { return *m_scriptSystem; }
		[[nodiscard]] script::PlayMode& GetPlayMode() noexcept { return m_playMode; }
		[[nodiscard]] const FrameStats& GetFrameStats() const noexcept { return m_currentStats; }

	private:
		void FixedUpdate(float fixedDeltaTime);
		void Update(float deltaTime);
		/// Draws the shown models of the scene that culling did not reject.
		void SubmitModels(const ComponentStore<ModelComponent>& drawn, render::FrameWriter& frame);

		/// Render thread entry point: draws submitted frames until rendering is stopped.
		void RenderThreadFunc();

		/// How Play starts for PlayMode: the startup scene for Play Project, the project's main module.
		/// Nothing when the startup scene cannot be read: the Play does not begin.
		[[nodiscard]] std::optional<script::PlayStart> MakePlayStart(script::PlayTarget target) const;

	private:
		platform::Window m_window;
		UniquePtr<render::RenderSystem> m_renderSystem;
		UniquePtr<PhysicsSystem> m_physicsSystem;
		UniquePtr<InputSystem> m_inputSystem;
		UniquePtr<script::ScriptSystem> m_scriptSystem;

		Camera m_camera;
		Scene m_scene;
		graphics::ModelManager m_models;
		Settings m_settings;
		render::ViewPort m_engineViewPort;
		/// Edit or Play: physics steps and entity scripts run only in Play.
		script::PlayMode m_playMode;
		/// The open project; none until one is opened.
		std::optional<project::Project> m_project;
		/// The program's, given to the scripts; null when they draw nothing.
		graphics::DebugDraw* m_scriptDebugDraw { nullptr };

		bool m_isRunning{ false };
		float m_fixedTimeStep{ 1.0f / 60.0f }; // 60 Hz physics step
		/// Time not yet stepped by physics.
		float m_physicsAccumulator{ 0.0f };
		/// core::getTimeStamp() of the last BeginFrame.
		uint64_t m_lastFrameTime{ 0 };

		// FPS counter statistics
		float m_frameCounterTime{ 0.0f };
		uint32_t m_frameCount{ 0 };
		FrameStats m_currentStats;

		OcclusionCullingSystem m_cullingSystem;

		// Update and render run in parallel: the update thread builds frame N+1 while the render
		// thread draws frame N; RenderSystem's frame ring hands snapshots over (see render/frame/FrameRing.hpp).
		std::thread m_renderThread;
	};

} // namespace elm
