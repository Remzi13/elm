#pragma once

#include "core/FileSystem.hpp"

#include <optional>

namespace elm::project {

    /// A file of the project's assets folder, "models/Cube.model": relative, '/' separated, never
    /// outside the folder (no "..", no root). Project::Resolve makes the file path of it.
    /// Empty: the asset has no file (a model that exists only in memory).
    class AssetPath {
    public:
        AssetPath() = default;

        /// The asset path of the text, '\' read as '/'; nothing for an empty, absolute or escaping path.
        [[nodiscard]] static std::optional<AssetPath> FromString(StringView text);

        [[nodiscard]] bool Empty() const noexcept { return m_text.empty(); }
        [[nodiscard]] const String& Text() const noexcept { return m_text; }
        /// The same path relative to the assets folder.
        [[nodiscard]] Path ToPath() const;
        /// The file of this path under an assets folder.
        [[nodiscard]] Path In(const Path& assetsFolder) const;
        /// The asset path of a file under an assets folder; nothing for a file outside of it.
        [[nodiscard]] static std::optional<AssetPath> Of(const Path& file, const Path& assetsFolder);

        [[nodiscard]] bool operator==(const AssetPath& other) const = default;

    private:
        explicit AssetPath(String text);

        String m_text;
    };

} // namespace elm::project
