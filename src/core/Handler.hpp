#pragma once

#include "core/Std.hpp"

namespace elm {
namespace core {

    class Handler final {
    public:
        enum Type {
            Core,
            Resource,
            Render,
            None
        };

        using ValueType = int;

    public:
        Handler() = default;

        Handler(int value, Type type)
            : m_type(type)
            , m_value(value)
        {
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept { return m_value >= 0; }

        constexpr bool operator==(const Handler&) const noexcept = default;
        constexpr auto operator<=>(const Handler&) const noexcept = default;
        
        Type GetType() const { return m_type; }

        ValueType GetValue() const { return m_value; }

    public:
        Type m_type { Type::None };
        ValueType m_value { -1 };
    };
}
}

template <>
struct std::hash<elm::core::Handler> {
    [[nodiscard]] std::size_t operator()(elm::core::Handler handler) const noexcept
    {
        const auto index = static_cast<std::size_t>(handler.GetValue());
        const auto type = static_cast<std::size_t>(handler.GetType());

        return index ^ (type + 0x9e3779b9u + (index << 6u) + (index >> 2u));
    }
};
