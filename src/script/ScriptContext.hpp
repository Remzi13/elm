#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"
#include "script/lua/Lua.hpp"

#include <cstdint>

namespace elm {
    class Camera;
    class InputSystem;
    class PhysicsSystem;
    class Scene;
    class Settings;
    class PlayModeController;
    namespace graphics {
        class ModelManager;
    }
}

namespace elm::script {

    class ScriptSystem;
    class ScriptModuleManager;
    class EntityScriptManager;
    class ScriptConsole;
    class ScriptReflection;

    /// Engine systems the bindings work with. Any of them may be missing (tools, tests):
    /// the functions that need it raise a Lua error then.
    struct EngineContext {
        Scene* scene { nullptr };
        graphics::ModelManager* models { nullptr };
        InputSystem* input { nullptr };
        Camera* camera { nullptr };
        Settings* settings { nullptr };
        PhysicsSystem* physics { nullptr };
        PlayModeController* playMode { nullptr };
    };

    /// Frame timing as scripts see it (elm.time).
    struct ScriptTime {
        double time { 0.0 };
        float deltaTime { 0.0f };
        float fixedDeltaTime { 0.0f };
        uint64_t frame { 0 };
    };

    /// Execution context associated with a Lua state.
    struct ScriptContext {
        EngineContext engine;
        ScriptTime time;
        ScriptSystem* system { nullptr };
        ScriptModuleManager* modules { nullptr };
        EntityScriptManager* entities { nullptr };
        ScriptConsole* console { nullptr };
        ScriptReflection* reflection { nullptr };
        PlayModeController* playMode { nullptr };

        [[nodiscard]] static ScriptContext& From(lua_State* L) noexcept;
    };

} // namespace elm::script
