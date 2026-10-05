#pragma once

#include "core/Std.hpp"

namespace elm::core {

    /// Typed resource handle: 24-bit slot index + 8-bit generation. Zero is the invalid handle.
    /// Handles are allocated on the update side (RenderResources) and stay valid for the
    /// render thread until the destroy command reaches it; a stale handle fails the generation check.
    template <typename Tag>
    class Handle {
    public:
        static constexpr uint32_t IndexBits = 24;
        static constexpr uint32_t IndexMask = (1u << IndexBits) - 1;
        static constexpr uint32_t MaxIndex = IndexMask;
    
        constexpr Handle() noexcept = default;
        constexpr Handle(uint32_t index, uint8_t generation) noexcept
            : m_value((static_cast<uint32_t>(generation) << IndexBits) | (index & IndexMask))
        {
        }
    
        [[nodiscard]] static constexpr Handle FromRaw(uint32_t value) noexcept
        {
            Handle handle;
            handle.m_value = value;
            return handle;
        }
    
        [[nodiscard]] constexpr bool IsValid() const noexcept { return m_value != 0; }
        constexpr explicit operator bool() const noexcept { return IsValid(); }
    
        [[nodiscard]] constexpr uint32_t Index() const noexcept { return m_value & IndexMask; }
        [[nodiscard]] constexpr uint8_t Generation() const noexcept { return static_cast<uint8_t>(m_value >> IndexBits); }
        [[nodiscard]] constexpr uint32_t Raw() const noexcept { return m_value; }
    
        constexpr bool operator==(const Handle&) const noexcept = default;
        constexpr auto operator<=>(const Handle&) const noexcept = default;
    
    private:
        uint32_t m_value { 0 };
    };

}

template <typename Tag>
struct std::hash<elm::core::Handle<Tag>> {
    [[nodiscard]] std::size_t operator()(elm::core::Handle<Tag> handle) const noexcept
    {
        return std::hash<uint32_t> { }(handle.Raw());
    }
};
