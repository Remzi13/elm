#pragma once

#include "core/Std.hpp"

#include "Scene/ComponentStore.hpp"

#include "script/lua/Ref.hpp"

#include <span>

namespace elm::script {

    /// The instance of a node's ScriptComponent.
    struct EntityScript {
        NodeId node;
        /// The module the instance was made from: a component naming another one gets a new instance.
        String module;
        /// The instance table, empty when it could not be made (error tells why).
        lua::Ref self;
        /// Tells this instance from one that took its place during a Lua call.
        uint64_t serial { 0 };
        bool started { false };
        /// Stopped by an error until a module reloads.
        bool faulted { false };
        /// Gone: on_destroy was called, the entry is dropped after the running pass.
        bool dead { false };
        String error;
    };

    /// The instances of the entity scripts in creation order, found by node. Passes over them nest
    /// through Lua calls (a hook spawns or destroys entities): while a pass runs, entries are only
    /// appended or marked dead, so a pass goes by index; DropDead removes the dead ones afterwards.
    /// Owner: EntityScripts, thread: update. A node without a live instance gives NoIndex.
    class EntityScriptList {
    public:
        static constexpr size_t NoIndex = ~size_t { 0 };

        /// A running pass while it lives: the entries keep their indices.
        class Pass {
        public:
            explicit Pass(EntityScriptList& list) noexcept : m_list(list) { ++m_list.m_passDepth; }
            ~Pass() { --m_list.m_passDepth; }

            Pass(const Pass&) = delete;
            Pass& operator=(const Pass&) = delete;

        private:
            EntityScriptList& m_list;
        };

        /// Appends the node's new instance with a fresh serial; returns its index.
        size_t Add(EntityScript script);
        /// Index of the node's live instance, NoIndex when it has none.
        [[nodiscard]] size_t FindIndex(NodeId node) const noexcept;
        [[nodiscard]] EntityScript& operator[](size_t index) noexcept { return m_scripts[index]; }
        [[nodiscard]] const EntityScript& operator[](size_t index) const noexcept { return m_scripts[index]; }
        [[nodiscard]] size_t Size() const noexcept { return m_scripts.size(); }
        [[nodiscard]] bool Empty() const noexcept { return m_scripts.empty(); }
        /// Dead entries included until DropDead.
        [[nodiscard]] std::span<const EntityScript> All() const noexcept { return m_scripts; }

        /// The entry stays in its place until DropDead.
        void MarkDead(size_t index) noexcept;
        /// Removes the dead entries, unless a pass is running.
        void DropDead();
        /// Forgets every entry; no hook is called.
        void Clear() noexcept;
        [[nodiscard]] bool IsPassRunning() const noexcept { return m_passDepth > 0; }

    private:
        Vector<EntityScript> m_scripts;
        /// NodeId::Raw() -> index of the node's latest instance.
        UnorderedMap<uint32_t, uint32_t> m_index;
        uint64_t m_nextSerial { 1 };
        uint32_t m_passDepth { 0 };
        bool m_hasDead { false };
    };

} // namespace elm::script
