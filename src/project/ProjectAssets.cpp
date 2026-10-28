#include "project/ProjectAssets.hpp"

#include "project/Project.hpp"

#include <algorithm>
#include <utility>

namespace elm::project {

    namespace {

        AssetEntry ScanFolder(const Path& folder, AssetPath path)
        {
            AssetEntry entry { .path = std::move(path), .name = String(folder.Filename().string().c_str()), .folder = true };
            std::error_code error;
            for (auto it = Directory(folder).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
                const Path file = it.CurrentPath();
                const String name(file.Filename().string().c_str());
                const auto childPath = AssetPath::FromString(entry.path.Empty() ? name : entry.path.Text() + "/" + name);
                std::error_code typeError;
                if (!childPath || name.starts_with("."))
                    continue;
                if (it.IsDirectory(typeError))
                    entry.children.push_back(ScanFolder(file, *childPath));
                else
                    entry.children.push_back({ .path = *childPath, .name = name, .type = AssetTypeOf(file) });
            }
            std::sort(entry.children.begin(), entry.children.end(), [](const AssetEntry& left, const AssetEntry& right) {
                return left.folder != right.folder ? left.folder : left.name < right.name;
            });
            return entry;
        }

        void CollectFiles(const AssetEntry& entry, AssetType type, Vector<AssetPath>& files)
        {
            if (!entry.folder && entry.type == type)
                files.push_back(entry.path);
            for (const AssetEntry& child : entry.children)
                CollectFiles(child, type, files);
        }

        /// "scripts/npc/guard.lua" -> "npc.guard", "scripts/pkg/init.lua" -> "pkg"; empty outside the scripts folder.
        String ModuleOf(const AssetPath& script)
        {
            const String prefix = String(Project::ScriptsFolder) + "/";
            const String& text = script.Text();
            if (!text.starts_with(prefix) || !text.ends_with(".lua"))
                return { };
            String module = text.substr(prefix.size(), text.size() - prefix.size() - StringView(".lua").size());
            if (module == "init")
                return { };
            if (module.ends_with("/init"))
                module.resize(module.size() - StringView("/init").size());
            std::replace(module.begin(), module.end(), '/', '.');
            return module;
        }

    } // namespace

    ProjectAssets::ProjectAssets(Path assetsFolder)
        : m_folder(std::move(assetsFolder))
    {
        Refresh();
    }

    void ProjectAssets::Refresh()
    {
        m_root = ScanFolder(m_folder, { });
    }

    Vector<AssetPath> ProjectAssets::FilesOf(AssetType type) const
    {
        Vector<AssetPath> files;
        CollectFiles(m_root, type, files);
        return files;
    }

    Vector<String> ProjectAssets::ScriptModules() const
    {
        Vector<String> modules;
        for (const AssetPath& script : FilesOf(AssetType::Script)) {
            if (String module = ModuleOf(script); !module.empty())
                modules.push_back(std::move(module));
        }
        std::sort(modules.begin(), modules.end());
        return modules;
    }

} // namespace elm::project
