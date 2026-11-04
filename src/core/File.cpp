#include "core/File.hpp"

#include "core/FilesystemPath.hpp"

#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <utility>

namespace elm {

    namespace {
        auto FileError(const char* what, const Path& path) -> std::unexpected<EngineError>
        {
            return std::unexpected(EngineError(ErrorCode::FileAccessFailed, std::string(what) + ": " + path.string()));
        }
    } // namespace

    auto File::Open(const Path& path, FileMode mode) -> EngineResult<void>
    {
        if (auto closed = Close(); !closed)
            return closed;
        m_path = path;
        m_mode = mode;
        m_buffer = MakeUnique<std::filebuf>();
        if (mode == FileMode::Read) {
            // A folder opens on Linux and only fails later
            std::error_code error;
            if (!FileSystem::IsRegularFile(path, error) || !m_buffer->open(ToFilesystemPath(path), std::ios::in | std::ios::binary))
                return FileError("Failed to open file for reading", path);
        } else if (!m_buffer->open(ToFilesystemPath(path), std::ios::out | std::ios::binary | std::ios::trunc)) {
            return FileError("Failed to open file for writing", path);
        }
        return {};
    }

    bool File::IsOpen() const
    {
        return m_buffer && m_buffer->is_open();
    }

    auto File::ReadAll() -> EngineResult<BinaryBlock>
    {
        if (m_mode != FileMode::Read || !IsOpen())
            return FileError("File is not open for reading", m_path);
        const std::streamoff size = m_buffer->pubseekoff(0, std::ios::end, std::ios::in);
        const std::streamoff start = m_buffer->pubseekpos(0, std::ios::in);
        if (size < 0 || start != 0)
            return FileError("Failed to get the size of file", m_path);

        BinaryBlock block(static_cast<size_t>(size));
        if (m_buffer->sgetn(block.Bytes().data(), static_cast<std::streamsize>(size)) != size)
            return FileError("Failed to read file", m_path);
        return block;
    }

    auto File::Write(const BinaryBlock& block) -> EngineResult<void>
    {
        const auto bytes = block.Bytes();
        return Write(StringView(bytes.data(), bytes.size()));
    }

    auto File::Write(StringView text) -> EngineResult<void>
    {
        if (m_mode != FileMode::Write || !IsOpen())
            return FileError("File is not open for writing", m_path);
        const auto size = static_cast<std::streamsize>(text.size());
        if (m_buffer->sputn(text.data(), size) != size)
            return FileError("Failed to write file", m_path);
        return {};
    }

    auto File::Flush() -> EngineResult<void>
    {
        if (m_mode != FileMode::Write || !IsOpen())
            return FileError("File is not open for writing", m_path);
        if (m_buffer->pubsync() != 0)
            return FileError("Failed to write file", m_path);
        return {};
    }

    auto File::Close() -> EngineResult<void>
    {
        if (!IsOpen())
            return {};
        if (!m_buffer->close())
            return FileError("Failed to close file", m_path);
        return {};
    }

} // namespace elm
