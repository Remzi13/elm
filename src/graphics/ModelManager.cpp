#include "graphics/ModelManager.hpp"

namespace elm::graphics {

    ModelManager::ModelManager(render::RenderResources& resources)
        : m_resources(&resources)
    {
    }

    ModelHandler ModelManager::Insert(Model model, Path file)
    {
        uint32_t index;
        if (m_freeSlots.empty()) {
            ELM_ASSERT(m_slots.size() < ModelHandler::MaxIndex);
            index = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        } else {
            index = m_freeSlots.back();
            m_freeSlots.pop_back();
        }

        auto& slot = m_slots[index];
        slot.meshes = CreateMeshes(model, { });
        slot.model.emplace(std::move(model));
        slot.file = std::move(file);
        return ModelHandler(index + 1, slot.generation);
    }

    Vector<ModelHandler> ModelManager::InsertAll(Vector<Model> models, std::span<const Path> files)
    {
        Vector<ModelHandler> handlers;
        handlers.reserve(models.size());
        for (size_t index = 0; index < models.size(); ++index)
            handlers.push_back(Insert(std::move(models[index]), index < files.size() ? files[index] : Path { }));
        return handlers;
    }

    bool ModelManager::Replace(ModelHandler handler, Model model)
    {
        auto* slot = GetSlot(handler);
        if (!slot)
            return false;
        slot->meshes = CreateMeshes(model, std::move(slot->meshes));
        slot->model.emplace(std::move(model));
        return true;
    }

    bool ModelManager::SetFile(ModelHandler handler, Path file)
    {
        auto* slot = GetSlot(handler);
        if (!slot)
            return false;
        slot->file = std::move(file);
        return true;
    }

    const Path* ModelManager::FileOf(ModelHandler handler) const noexcept
    {
        const auto* slot = GetSlot(handler);
        return slot && !slot->file.Empty() ? &slot->file : nullptr;
    }

    const Model* ModelManager::Get(ModelHandler handler) const noexcept
    {
        const auto* slot = GetSlot(handler);
        return slot ? &*slot->model : nullptr;
    }

    std::span<const render::Mesh> ModelManager::GetMeshes(ModelHandler handler) const noexcept
    {
        const auto* slot = GetSlot(handler);
        return slot ? std::span<const render::Mesh>(slot->meshes) : std::span<const render::Mesh>();
    }

    bool ModelManager::Remove(ModelHandler handler)
    {
        auto* slot = GetSlot(handler);
        if (!slot)
            return false;

        slot->meshes.clear();
        slot->model.reset();
        slot->file.Clear();
        ++slot->generation;
        m_freeSlots.push_back(handler.Index() - 1);
        return true;
    }

    void ModelManager::Clear()
    {
        for (uint32_t index = 0; index < m_slots.size(); ++index) {
            auto& slot = m_slots[index];
            if (!slot.model)
                continue;
            slot.meshes.clear();
            slot.model.reset();
            slot.file.Clear();
            ++slot.generation;
            m_freeSlots.push_back(index);
        }
    }

    size_t ModelManager::Size() const noexcept
    {
        return m_slots.size() - m_freeSlots.size();
    }

    [[nodiscard]] ModelManager::Slot* ModelManager::GetSlot(ModelHandler handler) noexcept
    {
        if (!handler.IsValid() || handler.Index() == 0 || handler.Index() > m_slots.size())
            return nullptr;
        auto& slot = m_slots[handler.Index() - 1];
        return slot.generation == handler.Generation() && slot.model ? &slot : nullptr;
    }

    [[nodiscard]] const ModelManager::Slot* ModelManager::GetSlot(ModelHandler handler) const noexcept
    {
        return const_cast<ModelManager*>(this)->GetSlot(handler);
    }

    [[nodiscard]] Vector<render::Mesh> ModelManager::CreateMeshes(const Model& model, Vector<render::Mesh> previous)
    {
        Vector<render::Mesh> meshes;
        meshes.reserve(model.submeshes.size());
        for (const auto& submesh : model.submeshes) {
            const auto reused = std::find_if(previous.begin(), previous.end(), [&](const render::Mesh& mesh) {
                return mesh.IsValid() && mesh.GetData() == submesh.data;
            });
            if (reused != previous.end())
                meshes.push_back(std::move(*reused));
            else if (submesh.data)
                meshes.emplace_back(*m_resources, submesh.data);
            else
                meshes.emplace_back();
        }
        return meshes;
    }

}
