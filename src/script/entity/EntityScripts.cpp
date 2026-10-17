#include "script/entity/EntityScripts.hpp"

#include "script/entity/ScriptClass.hpp"
#include "script/lua/State.hpp"
#include "script/values/Entity.hpp"

#include "Scene/PhysicsComponents.hpp"
#include "Scene/Scene.hpp"
#include "Scene/ScriptComponent.hpp"

#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"
#include "core/Unexpected.hpp"

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "EntityScripts";
        constexpr size_t NoIndex = EntityScriptList::NoIndex;

        /// Registry key of the hook helper.
        constexpr const char* CallHookKey = "elm.entity_script_call";

        /// call(self, name, ...): self:name(...) when the instance or its class has such a function.
        constexpr const char* CallHookSource = R"lua(
            return function(self, name, ...)
                local hook = self[name]
                if hook ~= nil then return hook(self, ...) end
            end
        )lua";

        const char* CollisionHook(const CollisionEvent& event)
        {
            const bool enter = event.kind == CollisionEvent::Kind::Enter;
            if (event.trigger)
                return enter ? "on_trigger_enter" : "on_trigger_exit";
            return enter ? "on_collision_enter" : "on_collision_exit";
        }

    } // namespace

    EntityScripts::EntityScripts(lua::State& state, ScriptClass& classes) noexcept
        : m_state(state)
        , m_classes(classes)
    {
    }

    void EntityScripts::Init(Scene* scene)
    {
        m_scene = scene;
        const bool installed = m_state.InstallHelper(CallHookKey, CallHookSource).has_value();
        ELM_ASSERT(installed && "the entity script helper is engine code and must load");
    }

    void EntityScripts::Shutdown() noexcept
    {
        Release();
        m_scene = nullptr;
    }

    // ── Passes ──────────────────────────────────────────────────────────────────

    void EntityScripts::Update(float deltaTime)
    {
        if (!m_state.IsOpen() || !m_scene)
            return;
        ELM_PROFILE_SCOPE_N("Entity Scripts Update");
        {
            EntityScriptList::Pass pass(m_scripts);
            EndGone();
            StartNew();
            // Instances made by these calls start on the next pass
            CallStepHook("on_update", deltaTime);
        }
        m_scripts.DropDead();
    }

    void EntityScripts::Reap()
    {
        if (!m_state.IsOpen() || !m_scene)
            return;
        {
            EntityScriptList::Pass pass(m_scripts);
            EndGone();
        }
        m_scripts.DropDead();
    }

    void EntityScripts::FixedUpdate(float fixedDeltaTime)
    {
        if (!m_state.IsOpen() || !m_scene)
            return;
        ELM_PROFILE_SCOPE_N("Entity Scripts FixedUpdate");
        {
            EntityScriptList::Pass pass(m_scripts);
            EndGone();
            CallStepHook("on_fixed_update", fixedDeltaTime);
        }
        m_scripts.DropDead();
    }

    void EntityScripts::DispatchCollisions(std::span<const CollisionEvent> events)
    {
        if (!m_state.IsOpen() || events.empty() || m_scripts.Empty())
            return;
        ELM_PROFILE_SCOPE_N("Entity Scripts Collisions");
        lua_State* L = m_state;
        {
            EntityScriptList::Pass pass(m_scripts);
            for (const auto& event : events) {
                const bool contact = event.kind == CollisionEvent::Kind::Enter && !event.trigger;
                for (int side = 0; side < 2; ++side) {
                    const NodeId self = side == 0 ? event.first : event.second;
                    const NodeId other = side == 0 ? event.second : event.first;
                    const size_t index = m_scripts.FindIndex(self);
                    if (index == NoIndex || !m_scripts[index].started || m_scripts[index].faulted)
                        continue;

                    lua::Push(L, Entity { other });
                    if (contact) {
                        // contact = { point, normal }, the normal pointing from this entity to the other one
                        lua_createtable(L, 0, 2);
                        lua::Push(L, event.point);
                        lua_setfield(L, -2, "point");
                        lua::Push(L, side == 0 ? event.normal : event.normal * -1.0f);
                        lua_setfield(L, -2, "normal");
                    }
                    CallHook(index, CollisionHook(event), contact ? 2 : 1);
                }
            }
        }
        m_scripts.DropDead();
    }

    void EntityScripts::EndGone()
    {
        for (size_t index = 0; index < m_scripts.Size(); ++index) {
            const auto& script = m_scripts[index];
            if (script.dead)
                continue;
            // Looked up again every time: on_destroy calls may change the scene
            const ScriptComponent* component = FindComponent(script.node);
            if (!component || component->module != script.module || m_scene->IndexOf(script.node) == Scene::NoNode)
                End(index);
        }
    }

    void EntityScripts::StartNew()
    {
        // Components added without Lua (scene files, C++) get their instances here
        if (const auto* components = m_scene->FindComponents<ScriptComponent>()) {
            Vector<NodeId> missing;
            for (const NodeId node : components->Ids()) {
                if (m_scripts.FindIndex(node) == NoIndex && m_scene->IndexOf(node) != Scene::NoNode)
                    missing.push_back(node);
            }
            for (const NodeId node : missing) {
                // A failure is kept as a faulted instance and reported
                if (Push(node))
                    lua_pop(m_state, 1);
            }
        }

        // Instances made by on_start calls wait for the next pass
        const size_t count = m_scripts.Size();
        for (size_t index = 0; index < count; ++index) {
            auto& script = m_scripts[index];
            if (script.started || script.faulted || script.dead)
                continue;
            // on_destroy follows exactly the instances whose on_start was called
            script.started = true;
            CallHook(index, "on_start");
        }
    }

    void EntityScripts::CallStepHook(const char* hook, float deltaTime)
    {
        const size_t count = m_scripts.Size();
        for (size_t index = 0; index < count; ++index) {
            const auto& script = m_scripts[index];
            if (script.started && !script.faulted && !script.dead) {
                lua_pushnumber(m_state, deltaTime);
                CallHook(index, hook, 1);
            }
        }
    }

    // ── Instances ───────────────────────────────────────────────────────────────

    auto EntityScripts::Push(NodeId node) -> EngineResult<void>
    {
        return PushInstance(node, OnFailure::Keep);
    }

    auto EntityScripts::PushNew(NodeId node) -> EngineResult<void>
    {
        return PushInstance(node, OnFailure::Forget);
    }

    auto EntityScripts::PushInstance(NodeId node, OnFailure onFailure) -> EngineResult<void>
    {
        lua_State* L = m_state;
        const ScriptComponent* component = FindComponent(node);
        if (!component) {
            lua_pushnil(L);
            return { };
        }
        if (const size_t index = m_scripts.FindIndex(node); index != NoIndex) {
            const auto& script = m_scripts[index];
            if (script.module == component->module) {
                if (!script.self)
                    return MakeUnexpected(EngineError(ErrorCode::ScriptError, script.error));
                script.self.Push(L);
                return { };
            }
            // The component names another module now
            End(index);
            component = FindComponent(node);
            if (!component) {
                lua_pushnil(L);
                return { };
            }
        }

        // Making the instance runs Lua (the module body), which may change the component
        const ScriptComponent copy = *component;
        auto made = m_classes.PushInstance(copy.module, node, copy.props);
        if (!made && onFailure == OnFailure::Forget)
            return made;

        EntityScript script;
        script.node = node;
        script.module = copy.module;
        if (made)
            script.self = lua::Ref(L, -1);
        const size_t index = m_scripts.Add(std::move(script));
        // Kept faulted: not made again before a module reloads
        if (!made)
            Fault(index, m_scripts[index].serial, "creating the instance", made.error().message);
        return made;
    }

    void EntityScripts::Destroy(std::span<const NodeId> nodes)
    {
        if (!m_state.IsOpen())
            return;
        {
            EntityScriptList::Pass pass(m_scripts);
            for (const NodeId node : nodes) {
                if (const size_t index = m_scripts.FindIndex(node); index != NoIndex)
                    End(index);
            }
        }
        m_scripts.DropDead();
    }

    void EntityScripts::DestroyAll()
    {
        if (!m_state.IsOpen())
            return;
        {
            EntityScriptList::Pass pass(m_scripts);
            // Instances made by on_destroy calls stay: their nodes are checked on the next pass
            const size_t count = m_scripts.Size();
            for (size_t index = 0; index < count; ++index)
                End(index);
        }
        m_scripts.DropDead();
    }

    void EntityScripts::Release() noexcept
    {
        m_scripts.Clear();
        m_reportedErrors.clear();
    }

    auto EntityScripts::Find(NodeId node) const noexcept -> const EntityScript*
    {
        const size_t index = m_scripts.FindIndex(node);
        return index == NoIndex ? nullptr : &m_scripts[index];
    }

    void EntityScripts::SetField(NodeId node, StringView name, const ScriptValue& value)
    {
        const size_t index = m_scripts.FindIndex(node);
        if (!m_state.IsOpen() || index == NoIndex || !m_scripts[index].self)
            return;
        lua_State* L = m_state;
        lua::StackGuard guard(L);
        m_scripts[index].self.Push(L);
        lua_pushlstring(L, name.data(), name.size());
        PushScriptValue(L, value);
        lua_rawset(L, -3);
        lua_pop(L, 1);
    }

    void EntityScripts::OnModuleReloaded(StringView name)
    {
        m_reportedErrors.clear();
        {
            EntityScriptList::Pass pass(m_scripts);
            const size_t count = m_scripts.Size();
            // Any reload may have fixed what stopped an instance (a module it uses, its own code)
            for (size_t index = 0; index < count; ++index) {
                auto& script = m_scripts[index];
                if (script.dead || !script.faulted)
                    continue;
                if (script.self) {
                    script.faulted = false;
                    script.error.clear();
                } else {
                    // The instance could not be made: the next pass makes it again
                    m_scripts.MarkDead(index);
                }
            }
            for (size_t index = 0; index < count; ++index) {
                const auto& script = m_scripts[index];
                if (script.started && !script.faulted && !script.dead && script.module == name)
                    CallHook(index, "on_script_reload");
            }
        }
        m_scripts.DropDead();
    }

    // ── Hooks ───────────────────────────────────────────────────────────────────

    const ScriptComponent* EntityScripts::FindComponent(NodeId node) const noexcept
    {
        const auto* components = m_scene ? m_scene->FindComponents<ScriptComponent>() : nullptr;
        return components ? components->Find(node) : nullptr;
    }

    void EntityScripts::CallHook(size_t index, const char* hook, int argumentCount)
    {
        // Entries keep their index while the call runs
        EntityScriptList::Pass pass(m_scripts);
        lua_State* L = m_state;
        const uint64_t serial = m_scripts[index].serial;

        // call(self, hook, arguments...): the helper, self and the name go below the arguments
        const int firstArgument = lua_gettop(L) - argumentCount + 1;
        lua_getfield(L, LUA_REGISTRYINDEX, CallHookKey);
        m_scripts[index].self.Push(L);
        lua_pushstring(L, hook);
        lua_rotate(L, firstArgument, 3);
        if (auto called = m_state.ProtectedCall(argumentCount + 2, 0); !called)
            Fault(index, serial, hook, called.error().message);
    }

    void EntityScripts::Fault(size_t index, uint64_t serial, const char* hook, const String& message)
    {
        auto& script = m_scripts[index];
        // The instance ended during the call and another took its place
        if (script.serial != serial)
            return;
        script.faulted = true;
        script.error = String(hook) + ": " + message;

        // Instances sharing a bug fail the same way: report it once until a module reloads
        if (!m_reportedErrors.insert(script.module + '\n' + script.error).second)
            return;
        const Scene::Node* node = m_scene ? m_scene->Find(script.node) : nullptr;
        ERROR_MESSAGE(log::Category::Script, LogSource, "Script '%s' of entity '%s' failed in %s, the instance is stopped until a module reloads:\n%s",
            script.module.c_str(), node ? node->name.c_str() : "?", hook, message.c_str());
    }

    void EntityScripts::End(size_t index)
    {
        EntityScriptList::Pass pass(m_scripts);
        auto& script = m_scripts[index];
        if (script.dead)
            return;
        // Dead first: an on_destroy that destroys its own entity does not end it twice
        m_scripts.MarkDead(index);
        if (script.started && !script.faulted && script.self)
            CallHook(index, "on_destroy");
        m_scripts[index].self.Reset();
    }

} // namespace elm::script
