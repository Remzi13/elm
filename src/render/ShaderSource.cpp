#include "render/ShaderSource.hpp"

#include "core/File.hpp"
#include "core/Std.hpp"
#include "core/FileSystem.hpp"

namespace elm::render {

    String loadShaderSource(StringView fileName)
    {
        const Path name { fileName };
        const Path paths[] = {
            FileSystem::Resolve("shaders") / name,
            FileSystem::Resolve("assets/shaders") / name
        };
        for (const auto& path : paths) {
            File file;
            if (!file.Open(path, FileMode::Read))
                continue;
            const auto source = file.ReadAll();
            if (source && source->Size() > 0)
                return String(*source->ReadText(0, source->Size()));
        }
        return {};
    }

} // namespace elm::render
