#pragma once

#include "core/Error.hpp"
#include "core/Settings.hpp"

#include "core/FrameClock.hpp"

#include "graphics/CameraController.hpp"
#include "graphics/FrameStats.hpp"
#include "graphics/GraphicsSystem.hpp"
#include "graphics/gizmo/GizmoSettings.hpp"
#include "graphics/debug/DebugDrawing.hpp"

#include "physics/PhysicsSystem.hpp"

#include "platform/PlatformSystem.hpp"

#include "resource/ResourceSystem.hpp"

#include "Scene/SceneSystem.hpp"
#include "Scene/TestScenes.hpp"

#include "script/ScriptSystem.hpp"

#include "editor/EditMode.hpp"
#include "editor/SceneDocument.hpp"
#include "editor/SceneEdits.hpp"
#include "editor/ui/EditorServices.hpp"
#include "editor/ui/ImGuiSystem.hpp"

#include "project/Project.hpp"

#include <optional>

namespace elm {

    namespace editor {
        class ProjectHubWindow;
    }

    /// The editor (docs/editor/Редактор.md), the root of its composition (docs/core/Системы.md): owns the systems
    /// of the game as fields, gives them their dependencies and runs the frame. It loads and sets up projects,
    /// scenes and models, draws its UI over the engine view: ImGui and its windows,
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

        /// Reads .settings, opens the window, starts rendering and physics; ImGui, the editor windows, the
        /// Project Hub (hidden until ShowProjectHub) and the debug visualizers.
        [[nodiscard]] auto Init() -> EngineResult<void>;
        /// The frame loop, until the main window closes.
        void Run();
        /// Saves the settings, frees the UI, ends the scripts and every system.
        void Shutdown();

        /// Opens a project from its folder or its .elmproject file. It is read first: when it cannot be read
        /// the open one stays. Its windows open with its startup scene, it becomes the last recent project.
        [[nodiscard]] auto OpenProject(const Path& folderOrFile) -> EngineResult<void>;
        void ShowProjectHub();
        /// The Settings tab of the Project window, in front.
        void ShowProjectSettings();
        /// The project file opened last time, empty when there is none.
        [[nodiscard]] Path LastProject() const;
        /// A test scene in the scene, added to what is there.
        void LoadTestScene(ScenePreset preset, uint32_t instanceCount);

        /// The program elm next to this one, with this one's extension; an error when it is not there.
        [[nodiscard]] static auto FindPlayerExecutable() -> EngineResult<Path>;

    private:
        static void RegisterSettings(Settings& settings);
        /// False when the main window closed.
        [[nodiscard]] bool RunFrame();
        /// The fixed physics steps (in Play) with the scripts, the frame's input and scripts, culling.
        void Simulate(float deltaTime);
        void Draw();
        /// Visualizers of the editor's debug channels: world axes, colliders, rays and contacts.
        void AddVisualizers();
        /// The UI of the frame, the scene views of the windows included.
        void BuildUi(render::FrameWriter& frame);

        /// Makes the project the open one: its scripts start. Precondition: no project is open.
        void StartProject(project::Project project);
        /// Stops Play, ends the scripts, clears the scene, models and bodies. Nothing when no project is open.
        void EndProject();
        /// The project's windows close, the menu shows no project.
        void CloseProjectWindows();
        /// The windows that outlive projects: SocLab, the viewport, Log, Profiler, Settings.
        void CreateEditorWindows();
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

        /// The systems, each after what it depends on: destroyed in the reverse order.
        Settings m_settings;
        platform::PlatformSystem m_platform;
        resource::ResourceSystem m_resources;
        SceneSystem m_scene;
        PhysicsSystem m_physics;
        graphics::GraphicsSystem m_graphics;
        /// The engine view's debug drawing: the gizmo, the visualizers, elm.debug of the scripts.
        graphics::DebugDrawing m_debugDraw;
        /// With Play and Stop (its PlayMode): physics steps and entity scripts run only in Play.
        script::ScriptSystem m_scripts;
        FrameClock m_clock;
        /// The fly camera of the engine view in Edit; off in Play, where the camera is the game's.
        CameraController m_cameraController;
        /// Edit and Play around PlayMode: the snapshot of the edited scene.
        EditMode m_editMode;
        /// Undo and redo of the user's edits of the scene.
        SceneEdits m_edits;

        // The editor's services for its windows (docs/core/Системы.md#Окна редактора)
        script::PlayRequests m_playRequests;
        ScriptInspection m_scriptInspection;
        Selection m_selection;
        GizmoSettings m_gizmo { GizmoOperation::Translate, GizmoSpace::World };
        /// Filled while the project windows are open.
        FileOpener m_files;
        SceneFileRequests m_sceneFile;
        /// The open scene as a file; there while the project windows are open.
        std::optional<SceneDocument> m_sceneDocument;
        /// The Profiler's, updated every frame.
        FrameStats m_frameStats;

        UniquePtr<ImGuiSystem> m_imgui;
        /// Owned by m_imgui: an editor window, it outlives projects.
        editor::ProjectHubWindow* m_hub { nullptr };
        /// Asked for by the windows, applied at the start of the next frame, outside of any window.
        std::optional<Path> m_requestedProject;
        bool m_initialized { false };
    };

} // namespace elm
