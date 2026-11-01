#pragma once

#include "core/Error.hpp"
#include "core/Settings.hpp"

#include "engine/IEngineLayer.hpp"

#include "graphics/debug/DebugDrawSystem.hpp"

#include "editor/ui/ImGuiSystem.hpp"

#include "project/Project.hpp"

#include <optional>

namespace elm {

	class Engine;

	namespace editor {
		class ProjectHubWindow;
	}

	/// The editor on top of the engine (docs/editor/Редактор.md): ImGui and its windows, the Project menu, the
	/// Project Hub and the recent projects; project switches, new projects, the entry, Run Standalone and
	/// Build Game; Edit mode, which Stop goes back to. Saves the settings when it shuts down.
	/// Owner: main, thread: update; OnFrameRendered: render.
	class EditorLayer final : public IEngineLayer {
	public:
		/// The project file opened last.
		static constexpr StringView LastProjectSetting = "Project/Last";
		/// Project files opened before, the last one first, one per line.
		static constexpr StringView RecentProjectsSetting = "Project/Recent";
		static constexpr size_t MaxRecentProjects = 8;
		/// The game alone (docs/project/Проекты.md#Плеер), built next to the editor: Run Standalone starts it,
		/// Build Game copies it as the game's program.
		static constexpr StringView PlayerProgram = "elm";

		explicit EditorLayer(Engine& engine);
		~EditorLayer() override;

		EditorLayer(const EditorLayer&) = delete;
		EditorLayer& operator=(const EditorLayer&) = delete;
		EditorLayer(EditorLayer&&) = delete;
		EditorLayer& operator=(EditorLayer&&) = delete;

		void RegisterSettings(Settings& settings) override;
		/// ImGui, the editor windows, the Project Hub (hidden until ShowProjectHub) and the debug visualizers.
		[[nodiscard]] auto Init() -> EngineResult<void> override;
		void Shutdown() override;

		/// The project's windows open with its startup scene, the menu shows it, it becomes the last and
		/// the first recent project.
		void OnProjectOpened(const project::Project& project) override;
		/// The project's windows close.
		void OnProjectClosed() override;

		/// Applies the project switch asked for since the last update; the camera takes input over the viewport.
		void Update() override;
		/// The UI of the frame, the scene views of the windows included.
		void BuildOverlay(render::FrameWriter& frame) override;
		/// Back to Edit: the scene the Play started from comes back.
		void RequestStop() override;
		/// The editor always draws debug primitives: the gizmo, the visualizers, elm.debug.
		[[nodiscard]] graphics::DebugDrawSystem* GetDebugDraw() override { return &m_debugDraw; }
		void OnFrameRendered(const render::RenderedFrameInfo& info) override;

		void ShowProjectHub();
		/// The program elm next to this one, with this one's extension; an error when it is not there.
		[[nodiscard]] static auto FindPlayerExecutable() -> EngineResult<Path>;
		/// The project file opened last time, empty when there is none.
		[[nodiscard]] Path LastProject() const;

	private:
		/// Visualizers of the editor's debug channels: world axes, colliders, rays and contacts.
		void AddVisualizers();
		void CreateProjectWindows(const project::Project& project, const Path& openedScene);
		/// The Project menu for a project, or for none.
		void UpdateProjectMenu(const project::Project* project);
		[[nodiscard]] Vector<Path> RecentProjects() const;
		/// Puts the project file first in the recent projects and makes it the last one.
		void RememberProject(const Path& file);

		/// Switches to the project asked for since the last update. When it cannot be read the open one stays.
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

		Engine& m_engine;
		Settings& m_settings;
		/// The engine view's debug drawing, given to the engine (GetDebugDraw).
		graphics::DebugDrawSystem m_debugDraw;
		UniquePtr<ImGuiSystem> m_imgui;
		/// Owned by m_imgui: an editor window, it outlives projects.
		editor::ProjectHubWindow* m_hub { nullptr };
		/// Asked for by the windows, applied at the start of the next update, outside of any window.
		std::optional<Path> m_requestedProject;
	};

} // namespace elm
