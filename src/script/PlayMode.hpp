#pragma once

#include "core/Settings.hpp"

#include "Scene/SceneAsset.hpp"

#include "script/EngineContext.hpp"

#include <functional>
#include <optional>

namespace elm {
    class SceneSnapshot;
}

namespace elm::script {

    class ScriptSystem;

    /// What Play is asked to play: the game as it starts, or the edited scene.
    enum class PlayTarget : uint8_t { Project, Scene };

    /// How a Play begins (docs/editor/Редактор.md): the scene to play in and the main module to start.
    struct PlayStart {
        /// Replaces the edited scene for the play (the startup scene); none: the edited scene is played.
        std::optional<SceneAsset> scene;
        /// Empty: no main module.
        String mainModule;
    };

    /// Makes the PlayStart of a target, nothing when it cannot (the error is logged). Given by the owner of
    /// the project: PlayMode knows no files.
    using PlayStartSource = std::function<std::optional<PlayStart>(PlayTarget)>;

    /// Edit and Play modes of the engine (docs/editor/Редактор.md). Edit: the scene is data, physics,
    /// entity scripts and the main module stand still. Play keeps a snapshot of the scene, gives the scripts
    /// a fresh Lua state, puts in the start scene, starts the main module and lets everything run.
    /// Stop ends the scripts, clears the physics bodies, brings the snapshot back with its node ids and
    /// gives Edit a fresh Lua state. It lives next to the scripts because it coordinates them with the
    /// scene and physics and the self-tests cover it there.
    /// Owner: EngineApp, thread: update. Requests from Lua and the UI are applied by ApplyRequest,
    /// at the start of the update.
    class PlayMode {
    public:
        static constexpr StringView PlayOnStartSetting = "PlayOnStart";

        PlayMode(Scene& scene, graphics::ModelManager* models, PhysicsSystem* physics, ScriptSystem& scripts) noexcept;
        ~PlayMode();

        PlayMode(const PlayMode&) = delete;
        PlayMode& operator=(const PlayMode&) = delete;
        PlayMode(PlayMode&&) = delete;
        PlayMode& operator=(PlayMode&&) = delete;

        static void RegisterSettings(Settings& settings);

        [[nodiscard]] bool IsPlaying() const noexcept { return m_playing; }
        /// Where requested Plays start from; without it a Play plays the edited scene without a main module.
        void SetStartSource(PlayStartSource source);
        /// Now. Not from inside a Lua call: requests there.
        void Play(PlayStart start);
        void Stop();
        void RequestPlay(PlayTarget target) noexcept;
        void RequestStop() noexcept { m_request = Request::Stop; }
        /// Play or Stop, as requested since the last update. A Play the start source gives no start for does not begin.
        void ApplyRequest();
        /// The requests for the scripts and the editor windows.
        [[nodiscard]] PlayRequests Requests();

    private:
        enum class Request : uint8_t { None, Play, Stop };

        /// The start scene in place of the edited one: the snapshot keeps the edited one.
        void ReplaceScene(SceneAsset asset);

        Scene& m_scene;
        graphics::ModelManager* m_models { nullptr };
        PhysicsSystem* m_physics { nullptr };
        ScriptSystem& m_scripts;
        Request m_request { Request::None };
        PlayTarget m_requestedTarget { PlayTarget::Project };
        PlayStartSource m_startSource;
        bool m_playing { false };
        /// The scene as Play found it, null in Edit mode.
        UniquePtr<SceneSnapshot> m_snapshot;
    };

} // namespace elm::script
