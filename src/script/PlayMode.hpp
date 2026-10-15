#pragma once

#include "core/Settings.hpp"

#include "script/EngineContext.hpp"

namespace elm {
    class SceneSnapshot;
}

namespace elm::script {

    class ScriptSystem;

    /// Edit and Play modes of the engine (docs/editor/Редактор.md). Edit: the scene is data, physics
    /// and entity scripts stand still. Play keeps a snapshot of the scene and lets everything run.
    /// Stop ends the entity scripts, clears the physics bodies and brings the snapshot back with its
    /// node ids. It lives next to the scripts because it coordinates them with the scene and physics
    /// and the self-tests cover it there.
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
        /// Now. Not from inside a Lua call: requests there.
        void Play();
        void Stop();
        void RequestPlay() noexcept { m_request = Request::Play; }
        void RequestStop() noexcept { m_request = Request::Stop; }
        /// Play or Stop, as requested since the last update.
        void ApplyRequest();
        /// The requests for the scripts and the editor windows.
        [[nodiscard]] PlayRequests Requests();

    private:
        enum class Request : uint8_t { None, Play, Stop };

        Scene& m_scene;
        graphics::ModelManager* m_models { nullptr };
        PhysicsSystem* m_physics { nullptr };
        ScriptSystem& m_scripts;
        Request m_request { Request::None };
        bool m_playing { false };
        /// The scene as Play found it, null in Edit mode.
        UniquePtr<SceneSnapshot> m_snapshot;
    };

} // namespace elm::script
