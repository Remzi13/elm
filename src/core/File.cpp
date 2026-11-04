#include "core/File.hpp"

#include "core/Log.hpp"
#include "core/Unexpected.hpp"

#include <ios>
#include <string>
#include <system_error>
#include <utility>

namespace elm {

    namespace {
        constexpr const char* LogSource = "File";

        auto FileError(const char* what, const Path& path) -> std::unexpected<EngineError>
        {
            return MakeUnexpected(ErrorCode::FileAccessFailed, std::string(what) + ": " + path.string(), log::Category::Core, LogSource);
        }
    } // namespace

    File::File(Path path, std::ifstream input, std::ofstream output)
        : m_path(std::move(path))
        , m_input(std::move(input))
        , m_output(std::move(output))
    {
    }

    auto File::OpenForReading(const Path& path) -> EngineResult<File>
    {
        // A folder opens as a stream on Linux and only fails later
        std::error_code error;
        if (!FileSystem::IsRegularFile(path, error))
            return FileError("Failed to open file for reading", path);
        auto input = FileSystem::OpenForReading(path, std::ios::binary);
        if (!input.is_open())
            return FileError("Failed to open file for reading", path);
        return File(path, std::move(input), std::ofstream {});
    }

    auto File::OpenForWriting(const Path& path) -> EngineResult<File>
    {
        if (const auto directory = path.Parent(); !directory.Empty()) {
            std::error_code error;
            FileSystem::CreateDirectories(directory, error);
            if (error)
                return FileError("Failed to create the folder of file", path);
        }
        auto output = FileSystem::OpenForWriting(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
            return FileError("Failed to open file for writing", path);
        return File(path, std::ifstream {}, std::move(output));
    }

    auto File::ReadAll() -> EngineResult<BinaryBlock>
    {
        if (!m_input.is_open())
            return FileError("File is not open for reading", m_path);
        m_input.clear();
        m_input.seekg(0, std::ios::end);
        const std::streamoff size = m_input.tellg();
        m_input.seekg(0, std::ios::beg);
        if (!m_input || size < 0)
            return FileError("Failed to get the size of file", m_path);

        BinaryBlock block(static_cast<size_t>(size));
        m_input.read(block.Bytes().data(), size);
        if (!m_input)
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
        if (!m_output.is_open())
            return FileError("File is not open for writing", m_path);
        m_output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!m_output)
            return FileError("Failed to write file", m_path);
        return {};
    }

    auto File::Close() -> EngineResult<void>
    {
        if (m_input.is_open())
            m_input.close();
        if (!m_output.is_open())
            return {};
        m_output.close();
        if (!m_output)
            return FileError("Failed to write file", m_path);
        return {};
    }

} // namespace elm
