#pragma once

#include "core/BinaryBlock.hpp"
#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include <cstdint>
#include <iosfwd>

namespace elm {

    /// What an open file does: the other kind of call is an error.
    enum class FileMode : uint8_t {
        /// An existing regular file.
        Read,
        /// Creates the file in an existing folder or empties an existing file.
        Write,
    };

    /// A file on disk, the only way to the contents of a file. Closes itself. Errors name the file and come back to the
    /// caller: File does not depend on the log, whoever calls it decides what to report. Calls on a file that is not
    /// open are errors. Owner: whoever made it. Any thread, one at a time.
    class File {
    public:
        /// An open file is closed first; if that fails, nothing is opened.
        [[nodiscard]] auto Open(const Path& path, FileMode mode) -> EngineResult<void>;
        [[nodiscard]] bool IsOpen() const;

        /// All the bytes of the file.
        [[nodiscard]] auto ReadAll() -> EngineResult<BinaryBlock>;
        [[nodiscard]] auto Write(const BinaryBlock& block) -> EngineResult<void>;
        [[nodiscard]] auto Write(StringView text) -> EngineResult<void>;
        /// Hands what was written to the system: others can read it, and a failed write shows here.
        [[nodiscard]] auto Flush() -> EngineResult<void>;
        /// A failed write may only show here. The destructor closes too, but cannot report it.
        [[nodiscard]] auto Close() -> EngineResult<void>;

    private:
        Path m_path;
        FileMode m_mode { FileMode::Read };
        UniquePtr<std::filebuf> m_buffer;
    };

} // namespace elm
