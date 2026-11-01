#pragma once

#include "core/Error.hpp"

#include "model/ModelCatalog.hpp"

#include "project/Project.hpp"

#include "Scene/SceneAsset.hpp"

#include <optional>

namespace elm::resource {

    /// The open project and the resources loaded for it: its models (ModelCatalog) and the files they come
    /// from, the startup scene read from disk (docs/core/Системы.md). Knows nothing of the GPU or the scripts.
    /// Owner: the program (Game, Editor), thread: update.
    class ResourceSystem {
    public:
        ResourceSystem() = default;

        ResourceSystem(const ResourceSystem&) = delete;
        ResourceSystem& operator=(const ResourceSystem&) = delete;
        ResourceSystem(ResourceSystem&&) = delete;
        ResourceSystem& operator=(ResourceSystem&&) = delete;

        /// The project of a project folder or its .elmproject file; a relative path starts at the engine root.
        [[nodiscard]] static auto ReadProject(const Path& folderOrFile) -> EngineResult<project::Project>;

        /// Makes the project the open one. Precondition: none is open (Close).
        void Open(project::Project project);
        /// Forgets the open project and every model. Nothing when no project is open.
        void Close();
        /// The open project; null until one is opened.
        [[nodiscard]] const project::Project* GetProject() const noexcept;
        /// The open project's entry in memory, what the next Play starts with; the file is the caller's.
        void SetEntry(project::ProjectEntry entry);
        /// The startup scene of the open project from its file. Precondition: the entry has a startup scene.
        [[nodiscard]] auto LoadStartupScene() const -> EngineResult<SceneAsset>;

        [[nodiscard]] ModelCatalog& GetModels() noexcept { return m_models; }
        [[nodiscard]] const ModelCatalog& GetModels() const noexcept { return m_models; }

    private:
        ModelCatalog m_models;
        std::optional<project::Project> m_project;
    };

} // namespace elm::resource
