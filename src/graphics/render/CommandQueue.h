#pragma once

#include <variant>

namespace elm {
namespace render {

    struct UploadTexture {
        Handler handler;
        TextureData data;
    };

    using CommandVariant = std::variant<UploadTexture>;

    class CommandList {
    public:
        explicit CommandList() { }

        CommandList(CommandList&&) noexcept = default;
        CommandList& operator=(CommandList&&) noexcept = default;

        CommandList(const CommandList&) noexcept = delete;
        CommandList& operator=(const CommandList&) noexcept = delete;

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
        Vector<CommandVariant> m_commands;
    };

    class CommandQueue { };

} // namespace render
} // namespace elm
