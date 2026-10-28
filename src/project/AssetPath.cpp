#include "project/AssetPath.hpp"

#include <algorithm>
#include <utility>

namespace elm::project {

    namespace {

        /// A part between separators: not empty, not "." or "..", no drive (':').
        bool IsValidSegment(StringView segment)
        {
            return !segment.empty() && segment != "." && segment != ".." && segment.find(':') == StringView::npos;
        }

    } // namespace

    AssetPath::AssetPath(String text)
        : m_text(std::move(text))
    {
    }

    std::optional<AssetPath> AssetPath::FromString(StringView text)
    {
        String normalized(text);
        for (char& character : normalized) {
            if (character == '\\')
                character = '/';
        }
        const StringView path(normalized);
        size_t start = 0;
        while (start <= path.size()) {
            const size_t end = (std::min)(path.find('/', start), path.size());
            if (!IsValidSegment(path.substr(start, end - start)))
                return std::nullopt;
            start = end + 1;
        }
        return AssetPath(std::move(normalized));
    }

    Path AssetPath::ToPath() const
    {
        return Path(m_text);
    }

    Path AssetPath::In(const Path& assetsFolder) const
    {
        return assetsFolder / ToPath();
    }

    std::optional<AssetPath> AssetPath::Of(const Path& file, const Path& assetsFolder)
    {
        // A file outside of the folder gets a relative path starting with "..", which no asset path has
        const Path relative = file.LexicallyNormal().LexicallyRelative(assetsFolder.LexicallyNormal());
        return FromString(relative.generic_string());
    }

} // namespace elm::project
