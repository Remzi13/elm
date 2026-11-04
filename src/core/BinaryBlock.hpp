#pragma once

#include "core/Std.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>

namespace elm {

    /// A value a BinaryBlock can hold: its bytes in memory are all of it.
    template <typename T>
    concept BinaryValue = std::is_trivially_copyable_v<T>;

    /// Owned bytes in a row: the contents of a file or what is about to be written into one. Values go in and out
    /// with their layout in memory. A read outside of the block reads nothing and says so.
    /// Owner: whoever holds it. Any thread, one at a time.
    class BinaryBlock {
    public:
        BinaryBlock() = default;
        /// A block of zero bytes.
        explicit BinaryBlock(size_t size);

        [[nodiscard]] size_t Size() const noexcept { return m_bytes.size(); }
        void Reserve(size_t size);
        [[nodiscard]] std::span<char> Bytes() noexcept { return m_bytes; }
        [[nodiscard]] std::span<const char> Bytes() const noexcept { return m_bytes; }
        /// The same bytes.
        [[nodiscard]] bool operator==(const BinaryBlock& other) const = default;

        /// Adds to the end of the block.
        template <BinaryValue T>
        void Append(const T& value)
        {
            AppendBytes(&value, sizeof(T));
        }
        template <BinaryValue T>
        void AppendArray(std::span<const T> values)
        {
            AppendBytes(values.data(), values.size_bytes());
        }
        void AppendText(StringView text);
        /// Adds zero bytes until the size is a multiple of alignment.
        void AlignTo(size_t alignment);

        /// The value at an offset, nothing if it is not all inside the block.
        template <BinaryValue T>
        [[nodiscard]] std::optional<T> Read(size_t offset) const
        {
            T value {};
            if (!CopyBytes(offset, 1, sizeof(T), &value))
                return std::nullopt;
            return value;
        }
        /// Fills values from an offset. False and nothing read if they are not all inside the block.
        template <BinaryValue T>
        [[nodiscard]] bool ReadArray(size_t offset, std::span<T> values) const
        {
            return CopyBytes(offset, values.size(), sizeof(T), values.data());
        }
        /// The text at an offset, valid while the block is unchanged; nothing if it is not all inside the block.
        [[nodiscard]] std::optional<StringView> ReadText(size_t offset, size_t size) const;

    private:
        void AppendBytes(const void* source, size_t size);
        [[nodiscard]] bool Contains(size_t offset, size_t count, size_t valueSize) const noexcept;
        [[nodiscard]] bool CopyBytes(size_t offset, size_t count, size_t valueSize, void* target) const;

        Vector<char> m_bytes;
    };

} // namespace elm
