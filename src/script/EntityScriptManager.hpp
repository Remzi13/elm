#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"
#include "Scene/ComponentStore.hpp"
#include "Scene/ScriptComponent.hpp"
#include "script/lua/Bind.hpp"

#include <span>

namespace elm {
    class Scene;
    struct CollisionEvent;
}

namespace elm::script {

    class ScriptModuleManager;

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

    class EntityScriptManager {
    public:
        EntityScriptManager() = default;
        ~EntityScriptManager() = default;

        EntityScriptManager(const EntityScriptManager&) = delete;
        EntityScriptManager& operator=(const EntityScriptManager&) = delete;

        void InstallHelpers(lua::State& state);

        void Update(lua::State& state, Scene* scene, ScriptModuleManager& modules, bool isPlaying, bool scriptsEnabled, float deltaTime);
        void FixedUpdate(lua::State& state, Scene* scene, bool isPlaying, bool scriptsEnabled, float fixedDeltaTime);

        [[nodiscard]] auto PushEntityScript(lua::State& state, Scene* scene, ScriptModuleManager& modules, NodeId node, bool keepFailure = true) -> EngineResult<void>;
        void DestroyEntityScripts(lua::State& state, Scene* scene, std::span<const NodeId> nodes);
        void DestroyAllEntityScripts(lua::State& state, Scene* scene);
        void DispatchCollisionEvents(lua::State& state, Scene* scene, std::span<const CollisionEvent> events);

        [[nodiscard]] const EntityScript* FindEntityScript(NodeId node) const noexcept;
        [[nodiscard]] size_t FindEntityScriptIndex(NodeId node) const noexcept;
        [[nodiscard]] std::span<const EntityScript> EntityScripts() const noexcept { return m_entityScripts; }

        void SetEntityScriptField(lua::State& state, NodeId node, StringView name, const ScriptValue& value);
        void OnModuleReloaded(lua::State& state, StringView name);
        void ReleaseEntityScripts() noexcept;

        [[nodiscard]] uint32_t PassDepth() const noexcept { return m_entityPassDepth; }

    private:
        void ReapEntityScripts(lua::State& state, Scene* scene);
        void StartEntityScripts(lua::State& state, Scene* scene, ScriptModuleManager& modules);
        [[nodiscard]] auto MakeEntityScript(lua::State& state, Scene* scene, ScriptModuleManager& modules, NodeId node, const ScriptComponent& component) -> EngineResult<void>;
        void CallEntityHook(lua::State& state, size_t index, const char* hook, int argumentCount = 0);
        void FaultEntityScript(size_t index, uint64_t serial, const char* hook, const String& message);
        void EndEntityScript(lua::State& state, size_t index, bool callOnDestroy);
        void DropDeadEntityScripts();

        Vector<EntityScript> m_entityScripts;
        UnorderedMap<uint32_t, uint32_t> m_entityScriptIndex;
        uint64_t m_nextEntitySerial { 1 };
        uint32_t m_entityPassDepth { 0 };
        bool m_hasDeadEntityScripts { false };
        Set<String> m_reportedEntityErrors;
    };

} // namespace elm::script
