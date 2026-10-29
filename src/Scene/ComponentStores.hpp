#pragma once

#include "core/Std.hpp"

#include "Scene/ComponentStore.hpp"
#include "Scene/ComponentTypes.hpp"
#include "Scene/NodeId.hpp"

#include <array>
#include <functional>

namespace elm {

class ComponentStores {
public:
    template <SceneComponent T>
    [[nodiscard]] ComponentStore<T>& Components()
    {
        auto& store = m_stores[IndexOf(T::Type)];
        if (!store)
            store = MakeUnique<ComponentStore<T>>();
        return static_cast<ComponentStore<T>&>(*store);
    }
    
    template <SceneComponent T>
    [[nodiscard]] const ComponentStore<T>* FindComponents() const noexcept
    {
        return static_cast<const ComponentStore<T>*>(m_stores[IndexOf(T::Type)].get());
    }

    
    void Remove(NodeId node);    
    void Copy(NodeId from, NodeId to);
    void Clear();
    [[nodiscard]] ComponentStores Clone() const;
    
    void TakeFrom(ComponentStores&& source, const std::function<NodeId(NodeId)>& remap);

private:
    [[nodiscard]] static constexpr size_t IndexOf(ComponentType type) noexcept { return static_cast<size_t>(type); }

    std::array<UniquePtr<IComponentStore>, AllComponentTypes.size()> m_stores;
};

} // namespace elm
