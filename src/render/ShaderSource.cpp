#include "render/ShaderSource.hpp"

#include <filesystem>
#include <fstream>

namespace elm::render {

String loadShaderSource(StringView fileName)
{
    const std::filesystem::path sourceRoot = std::filesystem::path { __FILE__ }.parent_path().parent_path().parent_path();
    const std::filesystem::path name { fileName };
    const std::filesystem::path paths[] = {
        std::filesystem::current_path() / "shaders" / name,
        std::filesystem::current_path() / "assets/shaders" / name,
        sourceRoot / "shaders" / name,
        sourceRoot / "assets/shaders" / name
    };
    for (const auto& path : paths) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            continue;
        const auto size = file.tellg();
        if (size <= 0 || !file.seekg(0))
            continue;
        String source(static_cast<size_t>(size), '\0');
        if (file.read(source.data(), size))
            return source;
    }
    return {};
}

} // namespace elm::render
