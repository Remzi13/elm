#include "resource/ProjectTemplate.hpp"

#include "resource/SerializationError.hpp"
#include "resource/Serializer.hpp"

#include "project/Project.hpp"

#include "core/File.hpp"

#include <cctype>

namespace elm::resource {

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
        FileSystem::CreateDirectories(scripts / ".meta", error);
        struct ScriptFile {
            Path path;
            StringView text;
        };
        const ScriptFile scriptFiles[] = {
            { scripts / "main.lua", MainModuleText },
            { scripts / ".luarc.json", LuaLsSettingsText },
            { scripts / ".meta" / "elm.lua", luaApi },
        };
        for (const ScriptFile& script : scriptFiles) {
            File scriptFile;
            if (auto opened = scriptFile.Open(script.path, FileMode::Write); !opened)
                return UnexpectedSerializationError(opened.error().message, LogSource);
            if (auto written = scriptFile.Write(script.text); !written)
                return UnexpectedSerializationError(written.error().message, LogSource);
            if (auto closed = scriptFile.Close(); !closed)
                return UnexpectedSerializationError(closed.error().message, LogSource);
        }
        if (error)
            return UnexpectedSerializationError("Cannot create the asset folders in " + folder.string(), LogSource);
        return file;
    }

} // namespace elm::resource
