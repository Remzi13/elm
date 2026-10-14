#include "script/ScriptReflection.hpp"
#include "script/ScriptModuleManager.hpp"
#include "script/bindings/ScriptValues.hpp"

#include "core/Unexpected.hpp"

#include <algorithm>

namespace elm::script {

    namespace {

        constexpr const char* PropsKey = "elm.entity_script_props";

    } // namespace

    auto ScriptReflection::ScriptDefaults(lua::State* state, ScriptModuleManager& modules, StringView module)
        -> const EngineResult<ScriptProps>&
    {
        const String name(module);
        if (const auto found = m_scriptDefaults.find(name); found != m_scriptDefaults.end())
            return found->second;

        auto read = [&]() -> EngineResult<ScriptProps> {
            if (!state)
                return MakeUnexpected(EngineError(ErrorCode::ScriptError, "scripts are not running"));
            lua_State* L = state->Get();
            lua::StackGuard guard(L);
            if (auto required = modules.Require(*state, name); !required)
                return MakeUnexpected(required.error());
            lua_getfield(L, LUA_REGISTRYINDEX, PropsKey);
            lua_insert(L, -2);
            lua::Push(L, name);
            if (auto called = state->ProtectedCall(2, 1); !called)
                return MakeUnexpected(called.error());

            ScriptProps props;
            const int table = lua_gettop(L);
            if (lua_istable(L, table)) {
                lua_pushnil(L);
                while (lua_next(L, table)) {
                    String key = lua_type(L, -2) == LUA_TSTRING ? String(lua_tostring(L, -2)) : String();
                    auto value = bindings::ToScriptValue(L, -1);
                    lua_pop(L, 1);
                    if (key.empty() || key == "entity" || !value) {
                        lua_settop(L, table - 1);
                        return MakeUnexpected(EngineError(ErrorCode::ScriptError,
                            name + ".props has a field that cannot be a prop" + (key.empty() ? String() : ": " + key)));
                    }
                    props.emplace_back(std::move(key), std::move(*value));
                }
            }
            lua_settop(L, table - 1);
            std::sort(props.begin(), props.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
            return props;
        };
        return m_scriptDefaults.emplace(name, read()).first->second;
    }

} // namespace elm::script
