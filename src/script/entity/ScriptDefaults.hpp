#pragma once

#include "core/Error.hpp"

#include "script/values/ScriptValues.hpp"

namespace elm::script {

    class ScriptClass;

    /// The defaults of the props of script classes, for the Inspector. Reading them loads the
    /// module, so a result is kept until Forget: after a module reload, or when the user asks again.
    /// Owner: ScriptSystem, thread: update.
    class ScriptDefaults {
    public:
        explicit ScriptDefaults(ScriptClass& classes) noexcept;

        /// The props of the module's class, sorted by name, or why they cannot be read.
        [[nodiscard]] auto Get(StringView module) -> const EngineResult<ScriptProps>&;
        void Forget() noexcept { m_cache.clear(); }

    private:
        ScriptClass& m_classes;
        /// Module name -> its defaults.
        UnorderedMap<String, EngineResult<ScriptProps>> m_cache;
    };

} // namespace elm::script
