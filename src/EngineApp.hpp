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

#include "EditorLayer.hpp"

#include <optional>
#include <thread>

namespace elm {

	struct ModelComponent;

	/// What the engine is started as (docs/project/Проекты.md#Плеер).
	enum class AppMode : uint8_t {
		/// The editor: EditorLayer with its windows, Edit and Play.
		Editor,
		/// The game alone (`--player`): no UI, the project plays from its start, Stop ends the program.
		Player
	};

	/// The engine: the systems of a running game, the open project and the frame loop. In AppMode::Editor
	/// it carries an EditorLayer; without one it is the player.
	class EngineApp {
	public:
		explicit EngineApp(AppMode mode);
		~EngineApp();

		EngineApp(const EngineApp&) = delete;
		EngineApp& operator=(const EngineApp&) = delete;
		EngineApp(EngineApp&&) noexcept = delete;
		EngineApp& operator=(EngineApp&&) noexcept = delete;

		[[nodiscard]] auto Init(uint32_t width = 1280, uint32_t height = 720, StringView title = "Elm") -> EngineResult<void>;

		[[nodiscard]] auto Run() -> EngineResult<void>;
		void Shutdown();

		void LoadTestScene(ScenePreset preset, uint32_t instanceCount = 1500);

		/// Opens the first project, from a project folder or its .elmproject file (a relative path starts
		/// at the engine root): its scripts start, the editor's windows open. Later switches come from the editor.
		[[nodiscard]] auto OpenProject(const Path& folderOrFile) -> EngineResult<void>;
		/// The project file the editor opened last time, empty when there is none (and in the player).
		[[nodiscard]] Path LastProject() const;
		/// Without a project the editor shows the Project Hub to create or open one.
		void ShowProjectHub();

	private:
		void FixedUpdate(float fixedDeltaTime);
		void Update(float deltaTime);
		/// The UI of the editor, or the game view of the player, into the frame's overlay.
		void BuildOverlay(render::FrameWriter& frame);
		void SubmitFrame(render::FrameWriter& frame);
		/// Draws the shown models of the scene that culling did not reject.
		void SubmitModels(const ComponentStore<ModelComponent>& drawn, render::FrameWriter& frame);

		/// Render thread entry point: draws submitted frames until rendering is stopped.
		void RenderThreadFunc();

		/// What the editor asks for: project switches, a new project, the entry, Run Standalone.
		[[nodiscard]] EditorRequests MakeEditorRequests();
		/// Visualizers of the editor's debug channels: world axes, colliders, rays and contacts.
		void AddEditorVisualizers();
		/// The Play requests the scripts get; in the player Stop ends the program.
		[[nodiscard]] script::PlayRequests MakePlayRequests();

		/// Switches to the project the editor asked for since the last update. The new project is
		/// read first: when it cannot be read the open one stays.
		void ApplyProjectRequest();
		/// Stops Play, ends the scripts, closes the project windows, clears the scene, models and bodies.
		void CloseProject();
		/// Makes the project the open one: its scripts start; the editor opens its startup scene and windows.
		void StartProject(project::Project project);
		/// Sets the open project's entry and writes its file (the Project window).
		[[nodiscard]] auto SetProjectEntry(project::ProjectEntry entry) -> EngineResult<void>;
		/// Makes a project from the template and asks to open it (Project Hub).
		[[nodiscard]] auto CreateProject(const Path& location, StringView name) -> EngineResult<void>;
		/// Starts this program as the player of the open project: `--project <file> --player`.
		void RunStandalone();
		/// Builds the open project into its Build/<Name> folder with this program as the game's (ProjectBuilder).
		void BuildGame();
		/// Puts the project's startup scene into the editor, if the project has one. Returns its file,
		/// empty when no scene was opened (none set, or the file could not be read: the error is logged).
		[[nodiscard]] Path OpenStartupScene();
		/// The startup scene of the project from its file. Precondition: the entry has a startup scene.
		[[nodiscard]] auto LoadStartupScene() const -> EngineResult<SceneAsset>;
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
		/// The open project; none until one is opened (the editor shows the Project Hub then).
		std::optional<project::Project> m_project;
		/// Asked for by the editor, applied at the start of the next update.
		std::optional<Path> m_requestedProject;
		/// The editor; null in the player.
		UniquePtr<EditorLayer> m_editor;

		bool m_isRunning{ false };
		/// The player's game asked to stop: the program ends after this frame.
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

} // namespace Engine
