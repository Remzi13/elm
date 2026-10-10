#include "render/ShaderSource.hpp"

#include "core/Std.hpp"
#include "core/FileSystem.hpp"

#include <fstream>

namespace elm::render {

String loadShaderSource(StringView fileName)
{
    const Path sourceRoot = Path{ __FILE__ }.Parent().Parent().Parent();
    const Path name { fileName };
    const Path paths[] = {
        FileSystem::CurrentPath() / "shaders" / name,
        FileSystem::CurrentPath() / "assets/shaders" / name,
        sourceRoot / "shaders" / name,
        sourceRoot / "assets/shaders" / name
    };
    for (const auto& path : paths) {
        auto file = FileSystem::OpenForReading(path, std::ios::binary | std::ios::ate);
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
