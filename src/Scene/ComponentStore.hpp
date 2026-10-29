#pragma once

#include "core/Debug.hpp"
#include "core/Std.hpp"

#include "Scene/NodeId.hpp"

#include <functional>
#include <span>
#include <type_traits>
#include <utility>

namespace elm {

class IComponentStore {
public:
    virtual ~IComponentStore() = default;
    [[nodiscard]] virtual bool Contains(NodeId id) const noexcept = 0;
    [[nodiscard]] virtual std::span<const NodeId> Ids() const noexcept = 0;
    virtual bool Remove(NodeId id) = 0;
    virtual void Clear() = 0;

    [[nodiscard]] virtual UniquePtr<IComponentStore> Clone() const = 0;
    virtual void Copy(NodeId from, NodeId to) = 0;
    
    [[nodiscard]] virtual UniquePtr<IComponentStore> MakeEmpty() const = 0;    
    virtual void TakeFrom(IComponentStore& source, const std::function<NodeId(NodeId)>& remap) = 0;
};

template <typename T>
class ComponentStore final : public IComponentStore {
public:
    template <typename... Args>
    T& Emplace(NodeId id, Args&&... args)
    {
        ELM_ASSERT(id && "a component needs a valid node id");
        const uint32_t slot = id.Index();
        if (slot < m_sparse.size() && m_sparse[slot] != None) {
            const uint32_t dense = m_sparse[slot];
            if (m_ids[dense] == id) {
                m_components[dense] = T(std::forward<Args>(args)...);
                return m_components[dense];
            }            
            RemoveAt(dense);
        }
        if (slot >= m_sparse.size())
            m_sparse.resize(slot + 1, None);
        m_sparse[slot] = static_cast<uint32_t>(m_ids.size());
        m_ids.push_back(id);
        return m_components.emplace_back(std::forward<Args>(args)...);
    }

    [[nodiscard]] T* Find(NodeId id) noexcept
    {
        const uint32_t dense = DenseIndex(id);
        return dense == None ? nullptr : &m_components[dense];
    }
    [[nodiscard]] const T* Find(NodeId id) const noexcept
    {
        const uint32_t dense = DenseIndex(id);
        return dense == None ? nullptr : &m_components[dense];
    }
    [[nodiscard]] bool Contains(NodeId id) const noexcept override { return DenseIndex(id) != None; }

    bool Remove(NodeId id) override
    {
        const uint32_t dense = DenseIndex(id);
        if (dense == None)
            return false;
        RemoveAt(dense);
        return true;
    }

    void Clear() override
    {
        m_components.clear();
        m_ids.clear();
        m_sparse.clear();
    }

    [[nodiscard]] UniquePtr<IComponentStore> Clone() const override
    {
        if constexpr (std::is_copy_constructible_v<T>) {
            return MakeUnique<ComponentStore<T>>(*this);
        } else {
            ELM_ASSERT(false && "a scene with components that cannot be copied has no snapshot");
            return nullptr;
        }
    }

    void Copy(NodeId from, NodeId to) override
    {
        if constexpr (std::is_copy_constructible_v<T>) {
            if (const T* source = Find(from)) {
                // A copy first: Emplace may move the components
                T copy = *source;
                Emplace(to, std::move(copy));
            }
        }
    }

    [[nodiscard]] UniquePtr<IComponentStore> MakeEmpty() const override { return MakeUnique<ComponentStore<T>>(); }

    void TakeFrom(IComponentStore& source, const std::function<NodeId(NodeId)>& remap) override
    {
        // The caller pairs stores of one component type
        auto& typed = static_cast<ComponentStore<T>&>(source);
        for (size_t dense = 0; dense < typed.m_ids.size(); ++dense) {
            if (const NodeId node = remap(typed.m_ids[dense]))
                Emplace(node, std::move(typed.m_components[dense]));
        }
        typed.Clear();
    }

    [[nodiscard]] size_t Size() const noexcept { return m_ids.size(); }
    [[nodiscard]] bool Empty() const noexcept { return m_ids.empty(); }
    
    [[nodiscard]] std::span<const NodeId> Ids() const noexcept override { return m_ids; }
    [[nodiscard]] std::span<T> Components() noexcept { return m_components; }
    [[nodiscard]] std::span<const T> Components() const noexcept { return m_components; }

private:
    static constexpr uint32_t None = ~0u;

    [[nodiscard]] uint32_t DenseIndex(NodeId id) const noexcept
    {
        if (!id || id.Index() >= m_sparse.size())
            return None;
        const uint32_t dense = m_sparse[id.Index()];

        return dense != None && m_ids[dense] == id ? dense : None;
    }

    void RemoveAt(uint32_t dense)
    {
        m_sparse[m_ids[dense].Index()] = None;
        const uint32_t last = static_cast<uint32_t>(m_ids.size() - 1);
        if (dense != last) {
            m_components[dense] = std::move(m_components[last]);
            m_ids[dense] = m_ids[last];
            m_sparse[m_ids[dense].Index()] = dense;
        }
        m_components.pop_back();
        m_ids.pop_back();
    }

    Vector<uint32_t> m_sparse;
    Vector<NodeId> m_ids;
    Vector<T> m_components;
};

} // namespace elm
