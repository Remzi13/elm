#pragma once

#include "core/Settings.hpp"

#include "Scene/SceneAsset.hpp"

#include "script/EngineContext.hpp"

#include <functional>
#include <optional>

namespace elm {
    class SceneSystem;
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

    /// Whether the game runs (docs/editor/Редактор.md). Stopped, physics, entity scripts and the main module
    /// stand still. Play gives the scripts a fresh Lua state, puts in the start scene, starts the main module
    /// and lets everything run, as a player starts the game. Stop ends the scripts and clears the physics
    /// bodies; the scene stays as the game left it. What comes after Stop is the program's: the editor brings
    /// the edited scene back (EditMode), the game ends. It lives next to the scripts because it coordinates
    /// them with the scene and physics and the self-tests cover it there.
    /// Owner: ScriptSystem (GetPlayMode), thread: update. Requests from Lua and the UI wait for the program: it looks at
    /// GetRequest and applies it (ApplyRequest) before the frame's fixed steps.
    class PlayMode {
    public:
        static constexpr StringView PlayOnStartSetting = "PlayOnStart";

        /// Without the scene a Play keeps the scene as it is, without physics a Stop clears no bodies.
        PlayMode(SceneSystem* scene, PhysicsSystem* physics, ScriptSystem& scripts) noexcept;
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
        /// What was asked for since the last ApplyRequest.
        enum class Request : uint8_t { None, Play, Stop };
        [[nodiscard]] Request GetRequest() const noexcept { return m_request; }
        /// Play or Stop, as requested. A Play the start source gives no start for does not begin; a Play while
        /// playing and a Stop while stopped do nothing.
        void ApplyRequest();
        /// The requests for the scripts and the editor windows.
        [[nodiscard]] PlayRequests Requests();

    private:

        /// The start scene in place of the edited one: the snapshot keeps the edited one.
        void ReplaceScene(SceneAsset asset);

        SceneSystem* m_scene { nullptr };
        PhysicsSystem* m_physics { nullptr };
        ScriptSystem& m_scripts;
        Request m_request { Request::None };
        PlayTarget m_requestedTarget { PlayTarget::Project };
        PlayStartSource m_startSource;
        bool m_playing { false };
    };

} // namespace elm::script
