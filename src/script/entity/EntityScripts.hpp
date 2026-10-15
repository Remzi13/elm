#pragma once

#include "core/Error.hpp"

#include "script/entity/EntityScriptList.hpp"
#include "script/values/ScriptValues.hpp"

#include <span>

namespace elm {
    class Scene;
    struct CollisionEvent;
    struct ScriptComponent;
}

namespace elm::script {

    class ScriptClass;

    /// The entity scripts. Every scene node with a ScriptComponent gets an instance of its module's
    /// class (ScriptClass): self = { entity = <Entity>, <props> } with the class as __index. Hooks, all optional:
    ///     on_start(self), on_update(self, dt), on_fixed_update(self, dt), on_destroy(self), on_script_reload(self)
    ///     on_collision_enter(self, other, contact), on_collision_exit(self, other),
    ///     on_trigger_enter(self, other), on_trigger_exit(self, other)
    /// An error stops only that instance, until a module reload. Hot reload patches the class table,
    /// so the instances run the new code and keep their fields.
    /// The components are the data and stay in the scene: an instance is made from its component, and
    /// one whose node or component went away ends (on_destroy) on the next pass.
    /// Owner: ScriptSystem, thread: update. Whether they run (Play, Script/Enabled) is the owner's call.
    class EntityScripts {
    public:
        EntityScripts(lua::State& state, ScriptClass& classes) noexcept;

        EntityScripts(const EntityScripts&) = delete;
        EntityScripts& operator=(const EntityScripts&) = delete;
        EntityScripts(EntityScripts&&) = delete;
        EntityScripts& operator=(EntityScripts&&) = delete;

        /// Installs the hook helper into a freshly opened Lua state. Without a scene nothing runs.
        void Init(Scene* scene);
        /// Forgets every instance without calling hooks: the Lua state is going away.
        void Shutdown() noexcept;

        /// Ends the instances that lost their node or component, starts the new ones, calls on_update.
        void Update(float deltaTime);
        /// Only ends the instances that lost their node or component (Edit mode).
        void Reap();
        void FixedUpdate(float fixedDeltaTime);
        /// on_collision_* / on_trigger_* of the scripts of both entities of every event (after a physics step).
        void DispatchCollisions(std::span<const CollisionEvent> events);

        /// Pushes the instance table of the node's script, nil for a node without a ScriptComponent.
        /// A component without an instance gets one now; its on_start waits for the next Update.
        /// When the instance cannot be made nothing is pushed and the error is returned; the failure
        /// is kept as a faulted instance, so it is not tried again before a module reload.
        [[nodiscard]] auto Push(NodeId node) -> EngineResult<void>;
        /// The same for a component just added: a failure is not kept, so the caller can take the
        /// component back.
        [[nodiscard]] auto PushNew(NodeId node) -> EngineResult<void>;
        /// Calls on_destroy of the started instances of these nodes, while the nodes still exist,
        /// and drops the instances. Before the nodes or their ScriptComponents are removed.
        void Destroy(std::span<const NodeId> nodes);
        /// The same for every instance: before the scene is cleared or replaced.
        void DestroyAll();
        /// Forgets every instance without calling hooks.
        void Release() noexcept;

        /// Instance of the node's script, nullptr when it has none.
        [[nodiscard]] const EntityScript* Find(NodeId node) const noexcept;
        /// Instances, in the order their hooks run (ended ones included until the pass ends).
        [[nodiscard]] std::span<const EntityScript> All() const noexcept { return m_scripts.All(); }
        /// A hook is running: the instances must not be released now.
        [[nodiscard]] bool IsPassRunning() const noexcept { return m_scripts.IsPassRunning(); }
        /// Writes a prop into the node's instance, so that a change in the Inspector shows at once.
        void SetField(NodeId node, StringView name, const ScriptValue& value);
        /// Any reload may have fixed what stopped an instance; on_script_reload for the module's instances.
        void OnModuleReloaded(StringView name);

    private:
        enum class OnFailure : uint8_t { Keep, Forget };

        [[nodiscard]] auto PushInstance(NodeId node, OnFailure onFailure) -> EngineResult<void>;
        [[nodiscard]] const ScriptComponent* FindComponent(NodeId node) const noexcept;
        void EndGone();
        /// Instances for the components that have none, then on_start for those not started.
        void StartNew();
        /// Calls the hook on every running instance with the time step.
        void CallStepHook(const char* hook, float deltaTime);
        /// Calls self:<hook>(...) in protected mode with the argumentCount values on top of the stack
        /// (they are popped); an error faults the instance.
        void CallHook(size_t index, const char* hook, int argumentCount = 0);
        void Fault(size_t index, uint64_t serial, const char* hook, const String& message);
        /// Marks the instance dead, then calls its on_destroy if it was started.
        void End(size_t index);

        lua::State& m_state;
        ScriptClass& m_classes;
        Scene* m_scene { nullptr };
        EntityScriptList m_scripts;
        /// Errors already logged since the last module reload: a bug shared by many instances is reported once.
        Set<String> m_reportedErrors;
    };

} // namespace elm::script
