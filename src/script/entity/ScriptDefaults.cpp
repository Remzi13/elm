#include "script/entity/ScriptDefaults.hpp"

#include "script/entity/ScriptClass.hpp"

namespace elm::script {

    ScriptDefaults::ScriptDefaults(ScriptClass& classes) noexcept
        : m_classes(classes)
    {
    }

    auto ScriptDefaults::Get(StringView module) -> const EngineResult<ScriptProps>&
    {
        String name(module);
        if (const auto found = m_cache.find(name); found != m_cache.end())
            return found->second;
        auto defaults = m_classes.ReadDefaults(name);
        return m_cache.emplace(std::move(name), std::move(defaults)).first->second;
    }

} // namespace elm::script
