#pragma once

#include "core/Debug.hpp"
#include "graphics/Model.hpp"
#include "render/api/RenderResources.hpp"

#include <optional>
#include <span>

namespace elm::graphics {

    /// Owns the models of a scene together with their GPU meshes. Every mesh of a model is created
    /// when the model is inserted and released when it is removed, replaced or the manager dies.
    class ModelManager {
    public:
        explicit ModelManager(render::RenderResources& resources);

        ModelManager(const ModelManager&) = delete;
        ModelManager& operator=(const ModelManager&) = delete;
        ModelManager(ModelManager&&) noexcept = default;
        ModelManager& operator=(ModelManager&&) noexcept = default;

        [[nodiscard]] ModelHandler Insert(Model model);

        /// Replaces the model behind a live handle. Meshes whose geometry is shared with
        /// the previous version are kept, the rest are created anew.
        bool Replace(ModelHandler handler, Model model);
        [[nodiscard]] const Model* Get(ModelHandler handler) const noexcept;
        
        /// GPU meshes of a model, one per submesh in the same order.
        [[nodiscard]] std::span<const render::Mesh> GetMeshes(ModelHandler handler) const noexcept;

        bool Remove(ModelHandler handler);
        void Clear();
        [[nodiscard]] size_t Size() const noexcept;

        /// Calls fn(const Model::Submesh&, render::MeshHandle) for every submesh of the model.
        template <typename Fn>
        void ForEachSubmesh(ModelHandler handler, Fn&& fn) const
        {
            const auto* slot = GetSlot(handler);
            if (!slot)
                return;
            const auto& submeshes = slot->model->submeshes;
            for (size_t index = 0; index < submeshes.size(); ++index)
                fn(submeshes[index], slot->meshes[index].GetHandle());
        }

        template<typename Fn>
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
            Vector<render::Mesh> meshes;
            uint8_t generation { 0 };
        };

        [[nodiscard]] Slot* GetSlot(ModelHandler handler) noexcept;
        [[nodiscard]] const Slot* GetSlot(ModelHandler handler) const noexcept;
        [[nodiscard]] Vector<render::Mesh> CreateMeshes(const Model& model, Vector<render::Mesh> previous);

        render::RenderResources* m_resources;
        Vector<Slot> m_slots;
        Vector<uint32_t> m_freeSlots;
    };

} // namespace elm::graphics
