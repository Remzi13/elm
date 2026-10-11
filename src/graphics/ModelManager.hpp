#pragma once

#include "core/Debug.hpp"
#include "graphics/Model.hpp"

#include <optional>

namespace elm::graphics {

    class ModelManager {
    public:
        
        [[nodiscard]] ModelHandler Insert(Model model)
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

            m_slots[index].model.emplace(std::move(model));
            return ModelHandler(index + 1, m_slots[index].generation);
        }

        [[nodiscard]] Model* Get(ModelHandler handler) noexcept
        {
            if (!handler.IsValid() || handler.Index() == 0 || handler.Index() > m_slots.size())
                return nullptr;
            auto& slot = m_slots[handler.Index() - 1];
            return slot.generation == handler.Generation() && slot.model ? &*slot.model : nullptr;
        }

        [[nodiscard]] const Model* Get(ModelHandler handler) const noexcept
        {
            return const_cast<ModelManager*>(this)->Get(handler);
        }

        bool Remove(ModelHandler handler)
        {
            Model* model = Get(handler);
            if (!model)
                return false;

            auto& slot = m_slots[handler.Index() - 1];
            slot.model.reset();
            ++slot.generation;
            m_freeSlots.push_back(handler.Index() - 1);
            return true;
        }

        void Clear()
        {
            for (uint32_t index = 0; index < m_slots.size(); ++index) {
                if (!m_slots[index].model)
                    continue;
                m_slots[index].model.reset();
                ++m_slots[index].generation;
                m_freeSlots.push_back(index);
            }
        }

        [[nodiscard]] size_t Size() const noexcept
        {
            return m_slots.size() - m_freeSlots.size();
        }

        template<typename Fn>
        void ForEach(Fn&& fn)
        {
            for (uint32_t index = 0; index < m_slots.size(); ++index) {
                auto& slot = m_slots[index];
                if (slot.model)
                    fn(ModelHandler(index + 1, slot.generation), *slot.model);
            }
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

        [[nodiscard]] UnorderedMap<ModelHandler, ModelHandler> MoveTo(ModelManager& destination)
        {
            UnorderedMap<ModelHandler, ModelHandler> remapping;
            remapping.reserve(Size());
            ForEach([&](ModelHandler sourceHandler, Model& model) {
                remapping.emplace(sourceHandler, destination.Insert(std::move(model)));
            });
            Clear();
            return remapping;
        }

    private:
        struct Slot {
            std::optional<Model> model;
            uint8_t generation { 0 };
        };

        Vector<Slot> m_slots;
        Vector<uint32_t> m_freeSlots;
    };

} // namespace elm::resmgr
