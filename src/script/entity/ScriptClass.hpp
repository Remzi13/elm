#pragma once

#include "core/Error.hpp"

#include "script/values/ScriptValues.hpp"

namespace elm::script {

    namespace lua {
        class State;
    }
    class ModuleLoader;

    /// A module as the class of entity scripts: a table with the methods of its instances and the
    /// defaults of their `props` (boolean, number, string, Vector3, Vector4, Entity). One Lua helper
    /// checks the class and its props for both the instances and the Inspector.
    /// Owner: ScriptSystem, thread: update.
    class ScriptClass {
    public:
        ScriptClass(lua::State& state, ModuleLoader& loader) noexcept;

        ScriptClass(const ScriptClass&) = delete;
        ScriptClass& operator=(const ScriptClass&) = delete;
        ScriptClass(ScriptClass&&) = delete;
        ScriptClass& operator=(ScriptClass&&) = delete;

        /// Installs the class helper into a freshly opened Lua state.
        void Init();

        /// Pushes a new instance of the module's class: self = { entity, the defaults of class.props
        /// (vectors copied), the overrides on top } with the class as __index. The module is loaded
        /// for it; nothing is pushed on failure.
        [[nodiscard]] auto PushInstance(StringView module, NodeId entity, const ScriptProps& overrides) -> EngineResult<void>;
        /// The defaults of class.props as prop values, sorted by name; the module is loaded for it.
        [[nodiscard]] auto ReadDefaults(StringView module) -> EngineResult<ScriptProps>;

    private:
        /// Pushes a function of the class helper: "new" or "props".
        void PushHelper(const char* function);

        lua::State& m_state;
        ModuleLoader& m_loader;
    };

} // namespace elm::script
