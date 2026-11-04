#pragma once

#include "core/Debug.hpp"
#include "core/FileSystem.hpp"
#include "model/Model.hpp"

#include <optional>
#include <span>

namespace elm {

    /// The models of a scene on the CPU and the files they are kept in, by handle. A file belongs to one model,
    /// so the nodes that use a file share its GPU meshes; files are told apart by their normal form
    /// (Path::LexicallyNormal). A model without a file exists only in memory (a primitive, an import). Knows
    /// nothing of the GPU: graphics::ModelMeshes makes the meshes of its models and follows its revisions. A
    /// stale handle finds nothing (nullptr, false). Owner: the scene's owner (Engine), thread: update.
    class ModelCatalog {
    public:
        ModelCatalog() = default;

        ModelCatalog(const ModelCatalog&) = delete;
        ModelCatalog& operator=(const ModelCatalog&) = delete;
        ModelCatalog(ModelCatalog&&) noexcept = default;
        ModelCatalog& operator=(ModelCatalog&&) noexcept = default;

        /// file: the .model file the model was read from, empty for a model that exists only in memory. A file
        /// the catalog has already stays with its model: `model` is dropped and that model's handle comes back.
        [[nodiscard]] ModelHandler Insert(Model model, Path file = {});
        /// Inserts every model with the file of the same index (files may be shorter); the handles come in
        /// the same order, the same file gives the same handle (a SceneAsset's models and modelFiles for Scene::Append).
        [[nodiscard]] Vector<ModelHandler> InsertAll(Vector<Model> models, std::span<const Path> files = {});

        /// Replaces the model behind a live handle, its file stays; its revision goes up.
        bool Replace(ModelHandler handler, Model model);
        /// Sets the file of a live model; an empty path: the model no longer matches a file (edited in memory).
        /// Another model that had this file no longer has one.
        bool SetFile(ModelHandler handler, Path file);
        /// The file of a model, nullptr for a stale handle or a model without a file.
        [[nodiscard]] const Path* FileOf(ModelHandler handler) const noexcept;
        /// The model of a file, an invalid handle when no model has it.
        [[nodiscard]] ModelHandler FindByFile(const Path& file) const;
        [[nodiscard]] const Model* Get(ModelHandler handler) const noexcept;
        /// Changes with every Insert and Replace of the model behind the handle: who copies a model (its
        /// GPU meshes) compares it to know when the copy is out of date. 0 for a stale handle.
        [[nodiscard]] uint32_t RevisionOf(ModelHandler handler) const noexcept;

        bool Remove(ModelHandler handler);
        void Clear();
        [[nodiscard]] size_t Size() const noexcept;

        template <typename Fn>
        void ForEach(Fn&& fn) const
        {
            for (uint32_t index = 0; index < m_slots.size(); ++index) {
                const auto& slot = m_slots[index];
                if (slot.model)
                    fn(ModelHandler(index + 1, slot.generation), *slot.model);
            }
        }

    private:
        struct Slot {
            std::optional<Model> model;
            Path file;
            uint32_t revision { 0 };
            uint8_t generation { 0 };
        };

        [[nodiscard]] Slot* GetSlot(ModelHandler handler) noexcept;
        [[nodiscard]] const Slot* GetSlot(ModelHandler handler) const noexcept;
        [[nodiscard]] ModelHandler FindByNormalFile(const Path& normalFile) const noexcept;
        [[nodiscard]] uint32_t NextRevision() noexcept { return ++m_lastRevision; }

        Vector<Slot> m_slots;
        Vector<uint32_t> m_freeSlots;
        /// Revisions are unique across slots: a model put in a reused slot never matches an old copy.
        uint32_t m_lastRevision { 0 };
    };

} // namespace elm
