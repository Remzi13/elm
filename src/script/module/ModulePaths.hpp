#pragma once

#include "core/FileSystem.hpp"

namespace elm::script {

    /// Where the modules of the script root live. A module name is a dotted path under the root:
    /// "a.b" -> a/b.lua or a/b/init.lua. No Lua here: only names and files.
    /// Owner: ScriptSystem, which sets the root in Init. Thread: any (reads the file system).
    class ModulePaths {
    public:
        void SetRoot(Path root) noexcept { m_root = std::move(root); }
        [[nodiscard]] const Path& Root() const noexcept { return m_root; }

        /// Path of the file a module name maps to, empty when no such file exists.
        [[nodiscard]] Path Find(StringView module) const;
        /// Module name of a script file: a/b.lua -> "a.b", a/init.lua -> "a";
        /// empty for a file outside the root or not a .lua file.
        [[nodiscard]] String NameOf(const Path& file) const;
        /// Names of the modules under the root ("npc.guard"), sorted. Folders starting with '.' are skipped.
        [[nodiscard]] Vector<String> List() const;
        /// "@relative/path.lua": errors and tracebacks show the file relative to the root.
        [[nodiscard]] String ChunkName(const Path& file) const;

        /// Module names are dotted identifiers: no path separators, no way out of the root.
        [[nodiscard]] static bool IsValidName(StringView module);
        /// Last write time of a module file, 0 when it cannot be read.
        [[nodiscard]] static int64_t WriteTimeOf(const Path& file);

    private:
        Path m_root;
    };

} // namespace elm::script
