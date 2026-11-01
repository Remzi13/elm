#pragma once

#include "core/Error.hpp"
#include "core/Settings.hpp"

#include "platform/Window.hpp"

#include "render/RenderSystem.hpp"
#include "render/ViewPort.hpp"

#include "graphics/debug/DebugDrawSystem.hpp"
#include "graphics/FrameStats.hpp"
#include "graphics/CameraController.hpp"
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

	class IEngineLayer;
	struct ModelComponent;

	/// The main window the engine opens.
	struct EngineDesc {
		uint32_t width { 1280 };
		uint32_t height { 720 };
		StringView title { "Elm" };
	};

	/// The engine: the systems of a running game, the open project and the frame loop. What the program
	/// adds on top (the editor, or the game view of the player) is its IEngineLayer.
	/// Owner: main. Thread: update (the main thread); the render thread is its own.
	class Engine {
	public:
		Engine();
		~Engine();

		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;
		Engine(Engine&&) noexcept = delete;
		Engine& operator=(Engine&&) noexcept = delete;

		/// The layer is kept until Shutdown and must outlive it.
		[[nodiscard]] auto Init(const EngineDesc& desc, IEngineLayer& layer) -> EngineResult<void>;

		[[nodiscard]] auto Run() -> EngineResult<void>;
		void Shutdown();

		void LoadTestScene(ScenePreset preset, uint32_t instanceCount = 1500);

		/// Opens a project from its folder or its .elmproject file (a relative path starts at the engine root):
		/// its scripts start. The new project is read first: when it cannot be read the open one stays,
		/// otherwise the open one is closed (Play stops, its scripts end, the scene is cleared).
		[[nodiscard]] auto OpenProject(const Path& folderOrFile) -> EngineResult<void>;
		/// The open project; null until one is opened.
		[[nodiscard]] const project::Project* OpenedProject() const;
		/// The open project's entry in memory, what the next Play starts with; the file is the caller's.
		void SetProjectEntry(project::ProjectEntry entry);
		/// The startup scene of the open project from its file. Precondition: the entry has a startup scene.
		[[nodiscard]] auto LoadStartupScene() const -> EngineResult<SceneAsset>;

		/// The frame loop ends after this frame.
		void RequestQuit() noexcept { m_quitRequested = true; }

		[[nodiscard]] Settings& GetSettings() noexcept { return m_settings; }
		[[nodiscard]] platform::Window& GetWindow() noexcept { return m_window; }
		[[nodiscard]] render::RenderSystem& GetRenderSystem() noexcept { return *m_renderSystem; }
		[[nodiscard]] render::ViewPort& GetViewPort() noexcept { return m_engineViewPort; }
		[[nodiscard]] Scene& GetScene() noexcept { return m_scene; }
		[[nodiscard]] graphics::ModelManager& GetModels() noexcept { return m_models; }
		[[nodiscard]] Camera& GetCamera() noexcept { return m_camera; }
		[[nodiscard]] CameraController& GetCameraController() noexcept { return m_cameraController; }
		[[nodiscard]] const OcclusionCullingSystem& GetCulling() const noexcept { return m_cullingSystem; }
		[[nodiscard]] graphics::DebugDrawSystem& GetDebugDraw() noexcept { return m_debugDraw; }
		[[nodiscard]] PhysicsSystem& GetPhysics() noexcept { return *m_physicsSystem; }
		[[nodiscard]] script::ScriptSystem& GetScripts() noexcept { return *m_scriptSystem; }
		[[nodiscard]] script::PlayMode& GetPlayMode() noexcept { return m_playMode; }
		[[nodiscard]] const FrameStats& GetFrameStats() const noexcept { return m_currentStats; }

	private:
		void FixedUpdate(float fixedDeltaTime);
		void Update(float deltaTime);
		void SubmitFrame(render::FrameWriter& frame);
		/// Draws the shown models of the scene that culling did not reject.
		void SubmitModels(const ComponentStore<ModelComponent>& drawn, render::FrameWriter& frame);

		/// Render thread entry point: draws submitted frames until rendering is stopped.
		void RenderThreadFunc();

		/// The Play requests the scripts get: Stop goes to the layer.
		[[nodiscard]] script::PlayRequests MakePlayRequests();
		/// Stops Play, ends the scripts, closes the project windows, clears the scene, models and bodies.
		void CloseProject();
		/// Makes the project the open one: its scripts start, the layer opens it.
		void StartProject(project::Project project);
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
		CameraController m_cameraController;
		Scene m_scene;
		graphics::ModelManager m_models;
		Settings m_settings;
		render::ViewPort m_engineViewPort;
		/// Edit or Play: physics steps and entity scripts run only in Play.
		script::PlayMode m_playMode;
		/// The open project; none until one is opened.
		std::optional<project::Project> m_project;
		/// The editor or the player; set by Init.
		IEngineLayer* m_layer { nullptr };

		bool m_isRunning{ false };
		/// Asked to stop: the program ends after this frame.
		bool m_quitRequested{ false };
		float m_fixedTimeStep{ 1.0f / 60.0f }; // 60 Hz physics step

		// FPS counter statistics
		float m_frameCounterTime{ 0.0f };
		uint32_t m_frameCount{ 0 };
		FrameStats m_currentStats;

		OcclusionCullingSystem m_cullingSystem;

		/// Debug primitives of the engine view, collected during the frame and copied into it.
		graphics::DebugDrawSystem m_debugDraw;

		// Update and render run in parallel: the update thread builds frame N+1 while the render
		// thread draws frame N; RenderSystem's frame ring hands snapshots over (see render/frame/FrameRing.hpp).
		std::thread m_renderThread;
	};

} // namespace elm
