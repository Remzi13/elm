#include "script/entity/EntityScriptList.hpp"

namespace elm::script {

    size_t EntityScriptList::Add(EntityScript script)
    {
        script.serial = m_nextSerial++;
        const size_t index = m_scripts.size();
        m_index[script.node.Raw()] = static_cast<uint32_t>(index);
        m_scripts.push_back(std::move(script));
        return index;
    }

    size_t EntityScriptList::FindIndex(NodeId node) const noexcept
    {
        const auto found = m_index.find(node.Raw());
        if (found == m_index.end() || m_scripts[found->second].dead)
            return NoIndex;
        return found->second;
    }

    void EntityScriptList::MarkDead(size_t index) noexcept
    {
        m_scripts[index].dead = true;
        m_hasDead = true;
    }

    void EntityScriptList::DropDead()
    {
        if (IsPassRunning() || !m_hasDead)
            return;
        m_hasDead = false;
        std::erase_if(m_scripts, [](const EntityScript& script) { return script.dead; });
        m_index.clear();
        for (size_t index = 0; index < m_scripts.size(); ++index)
            m_index[m_scripts[index].node.Raw()] = static_cast<uint32_t>(index);
    }

    void EntityScriptList::Clear() noexcept
    {
        m_scripts.clear();
        m_index.clear();
        m_hasDead = false;
    }

} // namespace elm::script
