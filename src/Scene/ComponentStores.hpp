#pragma once

#include "core/Std.hpp"

#include "Scene/ComponentStore.hpp"
#include "Scene/ComponentTypes.hpp"
#include "Scene/NodeId.hpp"

#include <array>
#include <functional>

namespace elm {

/// One store per ComponentType, each made on first use. Owners: Scene (the components of its nodes) and
/// SceneAsset (keyed by SceneAsset::NodeRef); the owner's thread rules apply. A store lives as long as
/// its owner: Clear empties it, references to it stay valid.
class ComponentStores {
public:
    /// Components of type T keyed by node id.
    template <SceneComponent T>
    [[nodiscard]] ComponentStore<T>& Components()
    {
        auto& store = m_stores[IndexOf(T::Type)];
        if (!store)
            store = MakeUnique<ComponentStore<T>>();
        return static_cast<ComponentStore<T>&>(*store);
    }
    /// nullptr while no component of type T was ever added.
    template <SceneComponent T>
    [[nodiscard]] const ComponentStore<T>* FindComponents() const noexcept
    {
        return static_cast<const ComponentStore<T>*>(m_stores[IndexOf(T::Type)].get());
    }

    /// Removes every component of the node.
    void Remove(NodeId node);
    /// Gives node `to` copies of the components of node `from`.
    void Copy(NodeId from, NodeId to);
    void Clear();
    [[nodiscard]] ComponentStores Clone() const;
    /// Moves every component of source to the node remap gives for its key; a component whose key remaps
    /// to an invalid id is dropped.
    void TakeFrom(ComponentStores&& source, const std::function<NodeId(NodeId)>& remap);

private:
    [[nodiscard]] static constexpr size_t IndexOf(ComponentType type) noexcept { return static_cast<size_t>(type); }

    std::array<UniquePtr<IComponentStore>, AllComponentTypes.size()> m_stores;
};

} // namespace elm
