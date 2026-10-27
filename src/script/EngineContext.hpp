#pragma once

#include <cstdint>
#include <functional>

namespace elm {
    class Camera;
    class InputSystem;
    class PhysicsSystem;
    class Scene;
    class Settings;
    namespace graphics {
        class DebugDraw;
        class ModelManager;
    }
}

namespace elm::script {

    /// Edit/Play switches for the scripts and the editor windows, given by PlayMode. They are
    /// requests: the mode changes at the start of the next update. Empty where the engine has no
    /// Play mode (tools, tests).
    struct PlayRequests {
        std::function<void()> play;
        std::function<void()> stop;
        std::function<bool()> isPlaying;
    };

    /// Engine systems the scripts work with. Any of them may be missing (tools, tests):
    /// the bindings that need it raise a Lua error then.
    struct EngineContext {
        Scene* scene { nullptr };
        graphics::ModelManager* models { nullptr };
        InputSystem* input { nullptr };
        Camera* camera { nullptr };
        Settings* settings { nullptr };
        PhysicsSystem* physics { nullptr };
        PlayRequests play;
        /// Debug drawing of the engine view (elm.debug).
        graphics::DebugDraw* debugDraw { nullptr };
    };

    /// Frame timing as scripts see it (elm.time).
    struct ScriptTime {
        double time { 0.0 };
        float deltaTime { 0.0f };
        float fixedDeltaTime { 0.0f };
        uint64_t frame { 0 };
    };

} // namespace elm::script
