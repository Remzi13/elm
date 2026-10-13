#pragma once

#include "script/ScriptSystem.hpp"

namespace elm::script::bindings {

    /// Registers every engine module: elm.log, elm.time, elm.settings, elm.math, elm.input,
    /// elm.scene, elm.models, elm.camera; print goes to the engine log.
    void RegisterAll(lua::State& state);

    void RegisterCore(lua::State& state);
    void RegisterMath(lua::State& state);
    void RegisterInput(lua::State& state);
    void RegisterScene(lua::State& state);

    /// An engine system from the script context; raises a Lua error when the engine
    /// runs without it (tools, tests).
    template <typename T>
    T& RequireSystem(lua_State* L, T* system, const char* name)
    {
        if (!system)
            luaL_error(L, "%s is not available here", name);
        return *system;
    }

    [[nodiscard]] inline const EngineContext& Engine(lua_State* L) noexcept { return ScriptSystem::From(L).Engine(); }

} // namespace elm::script::bindings
