#include "model/ModelCatalog.hpp"

namespace elm {

    ModelHandler ModelCatalog::Insert(Model model, Path file)
    {
        file = file.LexicallyNormal();
        if (const ModelHandler kept = FindByNormalFile(file))
            return kept;

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
        slot.model.emplace(std::move(model));
        slot.file = std::move(file);
        slot.revision = NextRevision();
        return ModelHandler(index + 1, slot.generation);
    }

    Vector<ModelHandler> ModelCatalog::InsertAll(Vector<Model> models, std::span<const Path> files)
    {
        Vector<ModelHandler> handlers;
        handlers.reserve(models.size());
        for (size_t index = 0; index < models.size(); ++index)
            handlers.push_back(Insert(std::move(models[index]), index < files.size() ? files[index] : Path { }));
        return handlers;
    }

    bool ModelCatalog::Replace(ModelHandler handler, Model model)
    {
        auto* slot = GetSlot(handler);
        if (!slot)
            return false;
        slot->model.emplace(std::move(model));
        slot->revision = NextRevision();
        return true;
    }

    bool ModelCatalog::SetFile(ModelHandler handler, Path file)
    {
        auto* slot = GetSlot(handler);
        if (!slot)
            return false;
        file = file.LexicallyNormal();
        if (auto* previous = GetSlot(FindByNormalFile(file)))
            previous->file.Clear();
        slot->file = std::move(file);
        return true;
    }

    const Path* ModelCatalog::FileOf(ModelHandler handler) const noexcept
    {
        const auto* slot = GetSlot(handler);
        return slot && !slot->file.Empty() ? &slot->file : nullptr;
    }

    ModelHandler ModelCatalog::FindByFile(const Path& file) const
    {
        return FindByNormalFile(file.LexicallyNormal());
    }

    const Model* ModelCatalog::Get(ModelHandler handler) const noexcept
    {
        const auto* slot = GetSlot(handler);
        return slot ? &*slot->model : nullptr;
    }

    uint32_t ModelCatalog::RevisionOf(ModelHandler handler) const noexcept
    {
        const auto* slot = GetSlot(handler);
        return slot ? slot->revision : 0;
    }

    bool ModelCatalog::Remove(ModelHandler handler)
    {
        auto* slot = GetSlot(handler);
        if (!slot)
            return false;

        slot->model.reset();
        slot->file.Clear();
        ++slot->generation;
        m_freeSlots.push_back(handler.Index() - 1);
        return true;
    }

    void ModelCatalog::Clear()
    {
        for (uint32_t index = 0; index < m_slots.size(); ++index) {
            auto& slot = m_slots[index];
            if (!slot.model)
                continue;
            slot.model.reset();
            slot.file.Clear();
            ++slot.generation;
            m_freeSlots.push_back(index);
        }
    }

    size_t ModelCatalog::Size() const noexcept
    {
        return m_slots.size() - m_freeSlots.size();
    }

    [[nodiscard]] ModelCatalog::Slot* ModelCatalog::GetSlot(ModelHandler handler) noexcept
    {
        if (!handler.IsValid() || handler.Index() == 0 || handler.Index() > m_slots.size())
            return nullptr;
        auto& slot = m_slots[handler.Index() - 1];
        return slot.generation == handler.Generation() && slot.model ? &slot : nullptr;
    }

    [[nodiscard]] const ModelCatalog::Slot* ModelCatalog::GetSlot(ModelHandler handler) const noexcept
    {
        return const_cast<ModelCatalog*>(this)->GetSlot(handler);
    }

    ModelHandler ModelCatalog::FindByNormalFile(const Path& normalFile) const noexcept
    {
        if (normalFile.Empty())
            return { };
        // As text: both are in their normal form, and Path::operator== makes a filesystem::path per comparison
        const StringView file(normalFile.c_str());
        for (uint32_t index = 0; index < m_slots.size(); ++index) {
            const auto& slot = m_slots[index];
            if (StringView(slot.file.c_str()) == file)
                return ModelHandler(index + 1, slot.generation);
        }
        return { };
    }

} // namespace elm
