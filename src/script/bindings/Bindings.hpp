#pragma once

#include "script/EngineContext.hpp"
#include "script/lua/Bind.hpp"

#include <functional>

namespace elm::script {
    class EntityScripts;
    class HotReload;
    class ModuleLoader;
}

namespace elm::script::bindings {

    // Each Register* adds one engine module to a Lua state with only the systems that module reaches,
    // kept in the state as the module's own context. A missing system (tools, tests) makes the
    // functions that need it raise a Lua error.

    /// elm.log and print: lines go to the engine log with the file and line of the caller.
    void RegisterLog(lua::State& state);
    /// elm.time: the frame timing its owner updates.
    void RegisterTime(lua::State& state, const ScriptTime& time);
    /// elm.settings: get and set by "Category/path".
    void RegisterSettingsModule(lua::State& state, Settings* settings);
    /// elm.script: reloads, restarts, persist, memory.
    void RegisterScriptModule(lua::State& state, ModuleLoader& modules, HotReload& hotReload, std::function<void()> requestRestart);
    void RegisterMath(lua::State& state);
    void RegisterInput(lua::State& state, InputSystem* input);
    /// elm.scene, the Entity class and the Script component.
    void RegisterScene(lua::State& state, Scene* scene, graphics::ModelManager* models, EntityScripts& entityScripts);
    /// elm.models and the Model class; model files are taken from the project's assets folder.
    void RegisterModels(lua::State& state, graphics::ModelManager* models, const project::Project* project);
    void RegisterCamera(lua::State& state, Camera* camera);
    /// elm.physics, the RigidBody and Collider components.
    void RegisterPhysics(lua::State& state, PhysicsSystem* physics, Scene* scene, graphics::ModelManager* models);

    /// elm.debug: lines and wireframe shapes in the engine view, in the channel Script/Default or
    /// in a channel the scripts register (elm.debug.channel).
    void RegisterDebug(lua::State& state, graphics::DebugDraw* debugDraw);
    /// Drops the timed debug primitives of every script channel: Play leaves none behind after Stop.
    void ClearScriptDebugDraw(lua::State& state);

    /// What elm.engine reaches: the scene and its models, the entity scripts that end before the
    /// scene is replaced, the Play/Stop requests and the project the scene files belong to.
    struct EngineModuleContext {
        Scene* scene { nullptr };
        graphics::ModelManager* models { nullptr };
        EntityScripts* entityScripts { nullptr };
        PlayRequests play;
        const project::Project* project { nullptr };
    };
    /// elm.engine: scene commands, the same as in the Scene Hierarchy window, and Play/Stop.
    void RegisterEngine(lua::State& state, EngineModuleContext context);

    /// The context a binding module registered (lua::State::SetContext); a Lua error without it.
    template <typename T>
    [[nodiscard]] T& ContextOf(lua_State* L)
    {
        T* context = lua::State::Context<T>(L);
        if (!context)
            luaL_error(L, "this engine module is not registered here");
        return *context;
    }

    /// An engine system from a binding context; raises a Lua error when the engine runs
    /// without it (tools, tests).
    template <typename T>
    T& RequireSystem(lua_State* L, T* system, const char* name)
    {
        if (!system)
            luaL_error(L, "%s is not available here", name);
        return *system;
    }

} // namespace elm::script::bindings
