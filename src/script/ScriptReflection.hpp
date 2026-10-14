#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"
#include "Scene/ScriptComponent.hpp"
#include "script/lua/Bind.hpp"

namespace elm::script {

    class ScriptModuleManager;

    using ScriptProps = Vector<std::pair<String, ScriptValue>>;

    class ScriptReflection {
    public:
        ScriptReflection() = default;
        ~ScriptReflection() = default;

        ScriptReflection(const ScriptReflection&) = delete;
        ScriptReflection& operator=(const ScriptReflection&) = delete;

        [[nodiscard]] auto ScriptDefaults(lua::State* state, ScriptModuleManager& modules, StringView module)
            -> const EngineResult<ScriptProps>&;

        void Invalidate(StringView module) { m_scriptDefaults.erase(String(module)); }
        void ForgetScriptDefaults() noexcept { m_scriptDefaults.clear(); }

    private:
        UnorderedMap<String, EngineResult<ScriptProps>> m_scriptDefaults;
    };

} // namespace elm::script
