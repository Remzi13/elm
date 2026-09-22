#pragma once

#include "graphics/render/Command.hpp"

#include <array>
#include <variant>

namespace elm {
namespace render {

    class CommandQueue {
    public:
        template <typename Command>
        void Push(Command&& command)
        {
            m_commandLists[m_writeIndex].Push(std::forward<Command>(command));
        }

        void BeginFrame()
        {
            m_executeIndex = m_writeIndex;

            m_writeIndex = (m_writeIndex + 1) % m_commandLists.size();

            m_commandLists[m_writeIndex].Clear();
        }

        template <class Executor>
        void Execute(Executor& executor)
        {
            m_commandLists[m_executeIndex].Execute(executor);            
        }

    private:
        std::array<CommandList, 3> m_commandLists;
        size_t m_writeIndex = 0;
        size_t m_executeIndex = 0;
    };

} // namespace render
} // namespace elm
