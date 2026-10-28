#pragma once

#include "core/Error.hpp"
#include "core/Settings.hpp"

#include "graphics/ui/ImGuiSystem.hpp"

#include "project/Project.hpp"

#include <functional>

namespace elm {

	class OcclusionCullingSystem;

	namespace editor {
		class ProjectHubWindow;
	}

	namespace platform {
		class Window;
	}

	namespace script {
		class ScriptSystem;
	}

	/// What the editor asks the engine for. Project switches are requests: the engine applies them at the
	/// start of the next update, outside of any window.
	struct EditorRequests {
		std::function<void(const Path& file)> openProject;
		/// Makes a project <location>/<name>/ and asks to open it.
		std::function<EngineResult<void>(const Path& location, StringView name)> createProject;
		/// Sets the open project's entry and writes its file.
		std::function<EngineResult<void>(project::ProjectEntry entry)> setEntry;
		/// Runs the open project in a player process of its own, from its files (Run Standalone).
		std::function<void()> runStandalone;
		/// Builds the open project into its Build/<Name> folder.
		std::function<void()> buildGame;
	};

	/// The editor on top of the engine (docs/editor/Редактор.md): ImGui and its windows, the Project menu, the
	/// Project Hub and the recent projects. EngineApp without it is the player (docs/project/Проекты.md#Плеер).
	/// Owner: EngineApp, thread: update; NotifyViewportSurfacesReleased: render.
	class EditorLayer {
	public:
		/// The project file opened last.
		static constexpr StringView LastProjectSetting = "Project/Last";
		/// Project files opened before, the last one first, one per line.
		static constexpr StringView RecentProjectsSetting = "Project/Recent";
		static constexpr size_t MaxRecentProjects = 8;

		EditorLayer(Settings& settings, EditorRequests requests);
		~EditorLayer();

		EditorLayer(const EditorLayer&) = delete;
		EditorLayer& operator=(const EditorLayer&) = delete;
		EditorLayer(EditorLayer&&) = delete;
		EditorLayer& operator=(EditorLayer&&) = delete;

		static void RegisterSettings(Settings& settings);

		/// ImGui, the editor windows and the Project Hub (hidden until ShowProjectHub).
		[[nodiscard]] auto Init(platform::Window& window, render::RenderSystem& renderSystem, const OcclusionCullingSystem& culling)
			-> EngineResult<void>;
		/// Before RenderSystem::Shutdown, with the render thread stopped.
		void Shutdown();

		/// The project's windows open, the menu shows it, it becomes the last and the first recent project.
		/// openedScene: the scene file the engine opened with it, empty for none.
		void OnProjectOpened(const project::Project& project, script::ScriptSystem& scripts, const Path& openedScene);
		/// The project's windows close.
		void OnProjectClosed();
		void ShowProjectHub();
		/// The project file opened last time, empty when there is none.
		[[nodiscard]] Path LastProject() const;

		/// Main thread: the UI of the frame, the scene views of the windows included.
		void BuildFrame(const ImGuiWindowContext& context);
		void NotifyViewportSurfacesReleased(uint64_t count);
		/// The engine viewport of the last UI frame: the editor camera takes input only then.
		[[nodiscard]] bool IsSceneViewportHovered() const noexcept;
		[[nodiscard]] bool IsSceneViewportFocused() const noexcept;

	private:
		void CreateProjectWindows(const project::Project& project, script::ScriptSystem& scripts, const Path& openedScene);
		/// The Project menu for a project, or for none.
		void UpdateProjectMenu(const project::Project* project);
		[[nodiscard]] Vector<Path> RecentProjects() const;
		/// Puts the project file first in the recent projects and makes it the last one.
		void RememberProject(const Path& file);

		Settings& m_settings;
		EditorRequests m_requests;
		UniquePtr<ImGuiSystem> m_imgui;
		/// Owned by m_imgui: an editor window, it outlives projects.
		editor::ProjectHubWindow* m_hub { nullptr };
	};

} // namespace elm
