#pragma once

#include "core/Std.hpp"

#include "graphics/Camera.hpp"

namespace elm {

    class SceneSnapshot;
    class SceneSystem;

    namespace script {
        class PlayMode;
        class ScriptSystem;
    } // namespace script

    /// Edit mode of the editor (docs/editor/Редактор.md): until Play the scene is data. It applies the
    /// Play/Stop requests of the windows and scripts around the PlayMode of the scripts: before a Play it keeps a
    /// snapshot of the edited scene and the view camera, after Stop it brings both back (the scene with its node
    /// ids) and gives Edit a fresh Lua state. The parts are the engine's and outlive it.
    /// Owner: Editor, thread: update, before the frame's fixed steps.
    class EditMode {
    public:
        EditMode(SceneSystem& scene, script::ScriptSystem& scripts, Camera& view);
        ~EditMode();

        EditMode(const EditMode&) = delete;
        EditMode& operator=(const EditMode&) = delete;
        EditMode(EditMode&&) = delete;
        EditMode& operator=(EditMode&&) = delete;

        /// The Play or Stop asked for since the last frame.
        void ApplyRequest();
        /// Back to Edit now, if playing: before the project closes.
        void Stop();

    private:
        void Play();
        /// Puts the edited scene and the view camera back and gives Edit a fresh Lua state.
        void RestoreEditedScene();

        script::PlayMode& m_play;
        SceneSystem& m_scene;
        script::ScriptSystem& m_scripts;
        /// The view camera: Play moves it (scripts, the scene's camera), Edit gets it back as Play found it.
        Camera& m_view;
        Camera m_editView;
        /// The edited scene as Play found it; null in Edit.
        UniquePtr<SceneSnapshot> m_snapshot;
    };

} // namespace elm
