#include "script/ScriptContext.hpp"
#include "script/lua/State.hpp"

namespace elm::script {

    ScriptContext& ScriptContext::From(lua_State* L) noexcept
    {
        return *lua::State::Context<ScriptContext>(L);
    }

} // namespace elm::script
