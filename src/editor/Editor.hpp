#pragma once

#include "core/Error.hpp"
#include "core/Settings.hpp"

#include "engine/Engine.hpp"

#include "graphics/CameraController.hpp"
#include "graphics/debug/DebugDrawSystem.hpp"

#include "editor/EditMode.hpp"
#include "editor/ui/ImGuiSystem.hpp"

#include "project/Project.hpp"

#include <optional>

namespace elm {

	namespace editor {
		class ProjectHubWindow;
	}

	/// The editor (docs/editor/Редактор.md): the program that owns the engine and runs its frame. It loads
	/// and sets up projects, scenes and models, draws its UI over the engine view: ImGui and its windows,
	/// the Project menu, the Project Hub and the recent projects; project switches, new projects, the entry,
	/// Run Standalone and Build Game; Edit mode, which Stop goes back to. Saves the settings when it shuts down.
	/// Owner: main. Thread: update (the main thread).
	class Editor {
	public:
		/// The project file opened last.
		static constexpr StringView LastProjectSetting = "Project/Last";
		/// Project files opened before, the last one first, one per line.
		static constexpr StringView RecentProjectsSetting = "Project/Recent";
		static constexpr size_t MaxRecentProjects = 8;
		/// The game alone (docs/project/Проекты.md#Плеер), built next to the editor: Run Standalone starts it,
		/// Build Game copies it as the game's program.
		static constexpr StringView PlayerProgram = "elm";

		Editor();
		/// Shuts down if Shutdown was not called.
		~Editor();

		Editor(const Editor&) = delete;
		Editor& operator=(const Editor&) = delete;
		Editor(Editor&&) = delete;
		Editor& operator=(Editor&&) = delete;

		/// The engine with the editor's settings and debug drawing, ImGui, the editor windows, the Project Hub
		/// (hidden until ShowProjectHub) and the debug visualizers.
		[[nodiscard]] auto Init() -> EngineResult<void>;
		/// The frame loop, until the main window closes.
		void Run();
		/// Saves the settings, frees the UI, shuts the engine down.
		void Shutdown();

		/// Opens a project from its folder or its .elmproject file. It is read first: when it cannot be read
		/// the open one stays. Its windows open with its startup scene, it becomes the last recent project.
		[[nodiscard]] auto OpenProject(const Path& folderOrFile) -> EngineResult<void>;
		void ShowProjectHub();
		/// The project file opened last time, empty when there is none.
		[[nodiscard]] Path LastProject() const;
		[[nodiscard]] Engine& GetEngine() noexcept { return m_engine; }

		/// The program elm next to this one, with this one's extension; an error when it is not there.
		[[nodiscard]] static auto FindPlayerExecutable() -> EngineResult<Path>;

	private:
		static void RegisterSettings(Settings& settings);
		/// Visualizers of the editor's debug channels: world axes, colliders, rays and contacts.
		void AddVisualizers();
		/// The UI of the frame, the scene views of the windows included.
		void BuildUi(render::FrameWriter& frame);

		/// The project's windows close, the menu shows no project.
		void CloseProjectWindows();
		/// The project's windows open with its startup scene, the menu shows it, it becomes the last project.
		void OpenProjectWindows(const project::Project& project);
		void CreateProjectWindows(const project::Project& project, const Path& openedScene);
		/// The Project menu for a project, or for none.
		void UpdateProjectMenu(const project::Project* project);
		[[nodiscard]] Vector<Path> RecentProjects() const;
		/// Puts the project file first in the recent projects and makes it the last one.
		void RememberProject(const Path& file);

		/// Switches to the project the windows asked for since the last frame; the error is logged.
		void ApplyProjectRequest();
		/// Makes a project <location>/<name>/ from the template and asks to open it (Project Hub).
		[[nodiscard]] auto CreateProject(const Path& location, StringView name) -> EngineResult<void>;
		/// Sets the open project's entry and writes its file (the Project window).
		[[nodiscard]] auto SetProjectEntry(project::ProjectEntry entry) -> EngineResult<void>;
		/// Starts the program elm for the open project: `elm --project <file>`.
		void RunStandalone();
		/// Builds the open project into its Build/<Name> folder with elm as the game's program (ProjectBuilder).
		void BuildGame();
		/// Puts the project's startup scene into the scene, if the project has one. Returns its file,
		/// empty when no scene was opened (none set, or the file could not be read: the error is logged).
		[[nodiscard]] Path OpenStartupScene(const project::Project& project);

		/// First: destroyed last, after everything that uses it.
		Engine m_engine;
		Settings& m_settings;
		/// The engine view's debug drawing: the gizmo, the visualizers, elm.debug of the scripts.
		graphics::DebugDrawSystem m_debugDraw;
		/// The fly camera of the engine view in Edit; off in Play, where the camera is the game's.
		CameraController m_cameraController;
		/// Edit and Play around the engine's PlayMode: the snapshot of the edited scene.
		EditMode m_editMode;
		UniquePtr<ImGuiSystem> m_imgui;
		/// Owned by m_imgui: an editor window, it outlives projects.
		editor::ProjectHubWindow* m_hub { nullptr };
		/// Asked for by the windows, applied at the start of the next frame, outside of any window.
		std::optional<Path> m_requestedProject;
	};

} // namespace elm
