#pragma once

#include "core/Std.hpp"

#include <cstdint>

namespace elm {
    class Scene;
    class SceneSnapshot;
    class PhysicsSystem;
    namespace graphics {
        class ModelManager;
    }
}

namespace elm::script {
    class ScriptSystem;
}

namespace elm {

    /// Coordinates Play and Edit modes across the engine:
    /// takes and restores scene snapshots, starts/stops physics bodies,
    /// and triggers script lifecycle callbacks (OnPlay, OnStop).
    class PlayModeController {
    public:
        enum class PlayRequest : uint8_t { None, Play, Stop };

        PlayModeController() = default;
        ~PlayModeController();

        PlayModeController(const PlayModeController&) = delete;
        PlayModeController& operator=(const PlayModeController&) = delete;

        [[nodiscard]] bool IsPlaying() const noexcept { return m_playing; }
        [[nodiscard]] PlayRequest GetRequest() const noexcept { return m_playRequest; }

        void RequestPlay() noexcept { m_playRequest = PlayRequest::Play; }
        void RequestStop() noexcept { m_playRequest = PlayRequest::Stop; }
        void ClearRequest() noexcept { m_playRequest = PlayRequest::None; }

        void Play(Scene* scene, graphics::ModelManager* models, PhysicsSystem* physics, script::ScriptSystem* scripts);
        void Stop(Scene* scene, graphics::ModelManager* models, PhysicsSystem* physics, script::ScriptSystem* scripts);
        void ApplyPendingRequests(Scene* scene, graphics::ModelManager* models, PhysicsSystem* physics, script::ScriptSystem* scripts);

        void ResetSnapshot() noexcept;

    private:
        bool m_playing { false };
        PlayRequest m_playRequest { PlayRequest::None };
        UniquePtr<SceneSnapshot> m_snapshot;
    };

} // namespace elm
