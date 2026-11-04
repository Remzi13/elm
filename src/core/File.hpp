#pragma once

#include "core/BinaryBlock.hpp"
#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include <fstream>

namespace elm {

    /// An open file that closes itself. How it was opened decides what it does: the other kind of call is an error.
    /// Errors name the file and go to the log. Owner: whoever opened it. Any thread, one at a time.
    class File {
    public:
        /// An existing file, anything else is an error.
        [[nodiscard]] static auto OpenForReading(const Path& path) -> EngineResult<File>;
        /// Creates the file with its folders or empties an existing one.
        [[nodiscard]] static auto OpenForWriting(const Path& path) -> EngineResult<File>;

        /// All the bytes of the file.
        [[nodiscard]] auto ReadAll() -> EngineResult<BinaryBlock>;
        [[nodiscard]] auto Write(const BinaryBlock& block) -> EngineResult<void>;
        [[nodiscard]] auto Write(StringView text) -> EngineResult<void>;
        /// A failed write may only show here. The destructor closes too, but cannot report it.
        [[nodiscard]] auto Close() -> EngineResult<void>;

    private:
        File(Path path, std::ifstream input, std::ofstream output);

        Path m_path;
        std::ifstream m_input;
        std::ofstream m_output;
    };

} // namespace elm
