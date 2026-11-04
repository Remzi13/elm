#include "core/BinaryBlock.hpp"

#include "core/Debug.hpp"

#include <cstring>

namespace elm {

    BinaryBlock::BinaryBlock(size_t size)
        : m_bytes(size)
    {
    }

    void BinaryBlock::Reserve(size_t size)
    {
        m_bytes.reserve(size);
    }

    void BinaryBlock::AppendText(StringView text)
    {
        AppendBytes(text.data(), text.size());
    }

    void BinaryBlock::AlignTo(size_t alignment)
    {
        ELM_ASSERT(alignment > 0 && "a size cannot be a multiple of zero");
        const size_t remainder = Size() % alignment;
        if (remainder != 0)
            m_bytes.resize(Size() + alignment - remainder);
    }

    std::optional<StringView> BinaryBlock::ReadText(size_t offset, size_t size) const
    {
        if (!Contains(offset, size, 1))
            return std::nullopt;
        return StringView(m_bytes.data() + offset, size);
    }

    void BinaryBlock::AppendBytes(const void* source, size_t size)
    {
        if (size == 0)
            return;
        const size_t end = Size();
        m_bytes.resize(end + size);
        std::memcpy(m_bytes.data() + end, source, size);
    }

    bool BinaryBlock::Contains(size_t offset, size_t count, size_t valueSize) const noexcept
    {
        // Division, not multiplication: a huge count from a damaged file must not overflow
        return offset <= Size() && count <= (Size() - offset) / valueSize;
    }

    bool BinaryBlock::CopyBytes(size_t offset, size_t count, size_t valueSize, void* target) const
    {
        if (!Contains(offset, count, valueSize))
            return false;
        if (count != 0)
            std::memcpy(target, m_bytes.data() + offset, count * valueSize);
        return true;
    }

} // namespace elm
