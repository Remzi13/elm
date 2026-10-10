#pragma once

#include "core/Std.hpp"

#include <array>
#include <filesystem>
#include <optional>

namespace elm::ui {

class FileDialog {
public:
    void Open(StringView title, StringView extension);
    void SaveAs(StringView title, StringView extension, StringView defaultFileName);
    [[nodiscard]] auto Draw() -> std::optional<std::filesystem::path>;

private:
    struct Entry {
        std::filesystem::path path;
        bool isDirectory { false };
    };

    void RefreshEntries();

    String m_title;
    String m_extension;
    String m_error;
    std::array<char, 260> m_saveFileName{};
    std::filesystem::path m_directory;
    std::filesystem::path m_selectedPath;
    Vector<Entry> m_entries;
    bool m_saveMode { false };
};

} // namespace elm::ui
