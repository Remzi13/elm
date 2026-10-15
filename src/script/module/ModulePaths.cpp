#include "script/module/ModulePaths.hpp"

#include <algorithm>

namespace elm::script {

    Path ModulePaths::Find(StringView module) const
    {
        if (!IsValidName(module))
            return { };
        String relative(module);
        for (char& c : relative) {
            if (c == '.')
                c = '/';
        }
        for (const char* suffix : { ".lua", "/init.lua" }) {
            Path candidate = m_root / (relative + suffix);
            std::error_code error;
            if (FileSystem::IsRegularFile(candidate, error))
                return candidate;
        }
        return { };
    }

    String ModulePaths::NameOf(const Path& file) const
    {
        const String relative(file.LexicallyRelative(m_root).generic_string().c_str());
        if (relative.empty() || relative.starts_with("..") || !relative.ends_with(".lua") || Path(file).IsRelative())
            return { };
        String name = relative.substr(0, relative.size() - 4);
        if (name == "init")
            return { };
        if (name.ends_with("/init"))
            name.resize(name.size() - 5);
        for (char& c : name) {
            if (c == '/')
                c = '.';
        }
        return IsValidName(name) ? name : String();
    }

    Vector<String> ModulePaths::List() const
    {
        Vector<String> modules;
        Vector<Path> folders { m_root };
        while (!folders.empty()) {
            const Path folder = std::move(folders.back());
            folders.pop_back();
            std::error_code error;
            for (auto it = Directory(folder).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
                const Path path = it.CurrentPath();
                std::error_code typeError;
                if (it.IsDirectory(typeError)) {
                    if (!path.Filename().string().starts_with("."))
                        folders.push_back(path);
                } else if (String module = NameOf(path); !module.empty()) {
                    modules.push_back(std::move(module));
                }
            }
        }
        std::sort(modules.begin(), modules.end());
        return modules;
    }

    String ModulePaths::ChunkName(const Path& file) const
    {
        return "@" + String(file.LexicallyRelative(m_root).generic_string().c_str());
    }

    bool ModulePaths::IsValidName(StringView module)
    {
        if (module.empty() || module.front() == '.' || module.back() == '.')
            return false;
        for (size_t index = 0; index < module.size(); ++index) {
            const char c = module[index];
            if (c == '/' || c == '\\' || c == ':' || (c == '.' && module[index + 1] == '.'))
                return false;
        }
        return true;
    }

    int64_t ModulePaths::WriteTimeOf(const Path& file)
    {
        std::error_code error;
        return FileSystem::LastWriteTime(file, error);
    }

} // namespace elm::script
