#pragma once

#include "core/Std.hpp"
#include "core/FileSystem.hpp"

#include <array>

#include <optional>

namespace elm::ui {

class FileDialog {
public:
    void Open(StringView title, StringView extension, const Path& directory = { });
    void SaveAs(StringView title, StringView extension, StringView defaultFileName, const Path& directory = { });
    /// Keeps the dialog in this folder and below it (the project's assets), until the next Open or SaveAs.
    void LimitTo(const Path& folder);
    [[nodiscard]] auto Draw() -> std::optional<Path>;

private:
    struct Entry {
        Path path;
        bool isDirectory { false };
    };

    void SetStartDirectory(const Path& directory);
    [[nodiscard]] bool IsInsideLimit(const Path& directory) const;
    void RefreshEntries();
    [[nodiscard]] static auto PathToString(const Path& path) -> String;

    String m_title;
    String m_extension;
    String m_error;
    std::array<char, 260> m_saveFileName { };
    Path m_directory;
    /// LimitTo's folder, empty: the whole disk.
    Path m_limit;
    Path m_selectedPath;
    Vector<Entry> m_entries;
    bool m_saveMode { false };
};

} // namespace elm::ui