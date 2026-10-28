#include "resmgr/ProjectTemplate.hpp"

#include "resmgr/SerializationError.hpp"
#include "resmgr/Serializer.hpp"

#include "project/Project.hpp"

#include <cctype>

namespace elm::resmgr {

    namespace {

        constexpr const char* LogSource = "ProjectTemplate";
        constexpr size_t MaxNameLength = 64;

        constexpr StringView MainModuleText =
            "-- The main module of the game (entry.mainModule): it runs in Play only, from a fresh Lua state.\n"
            "local main = {}\n"
            "\n"
            "function main.on_start()\n"
            "end\n"
            "\n"
            "function main.on_update(dt)\n"
            "end\n"
            "\n"
            "return main\n";

        constexpr StringView LuaLsSettingsText =
            "{\n"
            "    \"$schema\": \"https://raw.githubusercontent.com/LuaLS/vscode-lua/master/setting/schema.json\",\n"
            "    \"runtime.version\": \"Lua 5.4\",\n"
            "    \"runtime.builtin\": { \"io\": \"disable\", \"os\": \"disable\" },\n"
            "    \"workspace.library\": [ \".meta\" ]\n"
            "}\n";

        auto WriteText(const Path& file, StringView text) -> EngineResult<void>
        {
            std::error_code error;
            FileSystem::CreateDirectories(file.Parent(), error);
            auto stream = FileSystem::OpenForWriting(file, std::ios::out | std::ios::binary);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (error || !stream)
                return UnexpectedSerializationError("Cannot write " + file.string(), LogSource);
            return { };
        }

    } // namespace

    bool ProjectTemplate::IsValidName(StringView name)
    {
        if (name.empty() || name.size() > MaxNameLength)
            return false;
        for (const char character : name) {
            if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-')
                return false;
        }
        return true;
    }

    auto ProjectTemplate::Create(const Path& location, StringView name, StringView luaApi) -> EngineResult<Path>
    {
        if (!IsValidName(name))
            return UnexpectedSerializationError("A project name is letters, digits, '_' and '-': " + String(name), LogSource);
        const Path folder = location / name;
        if (FileSystem::Exists(folder))
            return UnexpectedSerializationError("The folder exists already: " + folder.string(), LogSource);

        Path file = folder / name;
        file += project::Project::FileExtension;
        const project::Project created(file, project::ProjectDesc { .name = String(name), .entry = { .mainModule = "main" } });
        if (auto saved = Serializer::SaveProject(created); !saved)
            return UnexpectedSerializationError(saved.error());

        std::error_code error;
        FileSystem::CreateDirectories(created.Scenes(), error);
        FileSystem::CreateDirectories(created.Models(), error);
        const Path scripts = created.Scripts();
        auto written = WriteText(scripts / "main.lua", MainModuleText);
        if (written)
            written = WriteText(scripts / ".luarc.json", LuaLsSettingsText);
        if (written)
            written = WriteText(scripts / ".meta" / "elm.lua", luaApi);
        if (!written)
            return UnexpectedSerializationError(written.error());
        if (error)
            return UnexpectedSerializationError("Cannot create the asset folders in " + folder.string(), LogSource);
        return file;
    }

} // namespace elm::resmgr
