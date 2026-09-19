#pragma once

#include <variant>
#include "core/Std.hpp"

namespace elm {
namespace render {

    template <typename TVariant>
    class BasicCommandList {
    public:
        explicit BasicCommandList() = default;

        BasicCommandList(BasicCommandList&&) noexcept = default;
        BasicCommandList& operator=(BasicCommandList&&) noexcept = default;

        BasicCommandList(const BasicCommandList&) = delete;
        BasicCommandList& operator=(const BasicCommandList&) = delete;

        void Reserve(std::size_t count)
        {
            m_commands.reserve(count);
        }

        template <typename Command>
        void Push(Command&& command)
        {
            m_commands.emplace_back(std::forward<Command>(command));
        }

        template <class Command, class... Args>
        void Emplace(Args&&... args)
        {
            m_commands.emplace_back(std::in_place_type<Command>, std::forward<Args>(args)...);
        }

        [[nodiscard]] std::size_t Size() const
        {
            return m_commands.size();
        }

        [[nodiscard]] bool Empty() const
        {
            return m_commands.empty();
        }

        void Clear()
        {
            m_commands.clear();
        }

        template <class Executor>
        void Execute(Executor& executor)
        {
            for (auto& command : m_commands) {
                std::visit([&executor](auto&& cmd) {
                    executor.Execute(cmd);
                }, command);
            }
        }

    private:
        Vector<TVariant> m_commands;
    };

} // namespace render
} // namespace elm
