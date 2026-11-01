#include "resource/ResourceSystem.hpp"

#include "core/Debug.hpp"
#include "core/Log.hpp"

#include "resource/Serializer.hpp"

#include <utility>

namespace elm::resource {

    namespace {

        constexpr const char* LogSource = "ResourceSystem";

    } // namespace

    auto ResourceSystem::ReadProject(const Path& folderOrFile) -> EngineResult<project::Project>
    {
        const Path file = project::Project::FindFile(FileSystem::Resolve(folderOrFile));
        auto loaded = Serializer::LoadProject(file);
        if (loaded && StringView(loaded->Name()) != StringView(file.Stem().string()))
            WARNING_MESSAGE(log::Category::Core, LogSource, "Project \"%s\" is in %s: the name and the file should match",
                loaded->Name().c_str(), file.Filename().string().c_str());
        return loaded;
    }

    void ResourceSystem::Open(project::Project project)
    {
        ELM_ASSERT(!m_project && "the open project is closed first (Close)");
        m_project = std::move(project);
        LOG_MESSAGE(log::Category::Core, LogSource, "Project %s opened from %s", m_project->Name().c_str(),
            m_project->File().string().c_str());
    }

    void ResourceSystem::Close()
    {
        if (!m_project)
            return;
        m_models.Clear();
        LOG_MESSAGE(log::Category::Core, LogSource, "Project %s closed", m_project->Name().c_str());
        m_project.reset();
    }

    const project::Project* ResourceSystem::GetProject() const noexcept
    {
        return m_project ? &*m_project : nullptr;
    }

    void ResourceSystem::SetEntry(project::ProjectEntry entry)
    {
        ELM_ASSERT(m_project && "the entry belongs to the open project");
        m_project->SetEntry(std::move(entry));
    }

    auto ResourceSystem::LoadStartupScene() const -> EngineResult<SceneAsset>
    {
        ELM_ASSERT(m_project && m_project->Desc().entry.startupScene && "the open project has a startup scene");
        const project::AssetPath& startupScene = *m_project->Desc().entry.startupScene;
        return Serializer::LoadScene(m_project->Resolve(startupScene), m_project->Assets());
    }

} // namespace elm::resource
