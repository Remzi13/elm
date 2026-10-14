#include "script/EntityScriptManager.hpp"
#include "script/ScriptModuleManager.hpp"
#include "script/bindings/Entity.hpp"
#include "script/bindings/ScriptValues.hpp"

#include "Scene/PhysicsComponents.hpp"
#include "Scene/TestScenes.hpp"

#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"
#include "core/Unexpected.hpp"

#include <algorithm>

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "EntityScriptManager";
        constexpr size_t NoIndex = ~size_t { 0 };

        constexpr const char* NewInstanceKey = "elm.entity_script_new";
        constexpr const char* CallHookKey = "elm.entity_script_call";
        constexpr const char* PropsKey = "elm.entity_script_props";

        constexpr const char* PropsSource = R"lua(
            return function(class, module)
                if type(class) ~= "table" then
                    error(("module '%s' returns %s: an entity script returns its class table"):format(module, type(class)), 0)
                end
                local props = class.props
                if props ~= nil and type(props) ~= "table" then
                    error(module .. ".props must be a table", 0)
                end
                return props
            end
        )lua";

        constexpr const char* NewInstanceSource = R"lua(
            local metas = setmetatable({}, { __mode = "k" })

            local function meta_of(class, module)
                local meta = metas[class]
                if meta == nil then
                    meta = {
                        __index = class,
                        __tostring = function(self)
                            return "Script(" .. module .. ", " .. tostring(rawget(self, "entity")) .. ")"
                        end,
                    }
                    metas[class] = meta
                end
                return meta
            end

            return function(class, entity, overrides, kind_of, module)
                if type(class) ~= "table" then
                    error(("module '%s' returns %s: an entity script returns its class table"):format(module, type(class)), 0)
                end
                local defaults = class.props
                if defaults ~= nil and type(defaults) ~= "table" then
                    error(module .. ".props must be a table", 0)
                end
                local self = {}
                for key, value in pairs(defaults or {}) do
                    if type(key) ~= "string" then
                        error(module .. ".props: keys must be strings", 0)
                    end
                    if key == "entity" then
                        error(module .. ".props.entity: the name is taken by self.entity", 0)
                    end
                    local kind = kind_of(value)
                    if kind == nil then
                        error(("%s.props.%s: %s cannot be a prop (boolean, number, string, Vector3, Vector4, Entity)")
                            :format(module, key, type(value)), 0)
                    end
                    if kind == "Vector3" or kind == "Vector4" then value = value:copy() end
                    self[key] = value
                end
                for key, value in pairs(overrides) do
                    local default = defaults and defaults[key]
                    if default ~= nil and kind_of(default) ~= kind_of(value) then
                        error(("%s.props.%s: %s expected, got %s"):format(module, key, kind_of(default), kind_of(value)), 0)
                    end
                    self[key] = value
                end
                self.entity = entity
                return setmetatable(self, meta_of(class, module))
            end
        )lua";

        constexpr const char* CallHookSource = R"lua(
            return function(self, name, ...)
                local hook = self[name]
                if hook ~= nil then return hook(self, ...) end
            end
        )lua";

        class PassScope {
        public:
            explicit PassScope(uint32_t& depth) noexcept : m_depth(depth) { ++m_depth; }
            ~PassScope() { --m_depth; }

            PassScope(const PassScope&) = delete;
            PassScope& operator=(const PassScope&) = delete;

        private:
            uint32_t& m_depth;
        };

    } // namespace

    void EntityScriptManager::InstallHelpers(lua::State& state)
    {
        lua_State* L = state.Get();
        for (const auto& [key, source] : { std::pair { NewInstanceKey, NewInstanceSource },
                                            std::pair { CallHookKey, CallHookSource },
                                            std::pair { PropsKey, PropsSource } }) {
            const bool installed = state.Load(source, "=engine") && state.ProtectedCall(0, 1);
            ELM_ASSERT(installed);
            if (installed)
                lua_setfield(L, LUA_REGISTRYINDEX, key);
        }
    }

    void EntityScriptManager::Update(lua::State& state, Scene* scene, ScriptModuleManager& modules, bool isPlaying, bool scriptsEnabled, float deltaTime)
    {
        if (!scene || !scriptsEnabled)
            return;
        ELM_PROFILE_SCOPE_N("Entity Scripts Update");
        {
            PassScope pass(m_entityPassDepth);
            ReapEntityScripts(state, scene);
            if (isPlaying) {
                StartEntityScripts(state, scene, modules);
                const size_t count = m_entityScripts.size();
                for (size_t index = 0; index < count; ++index) {
                    const auto& script = m_entityScripts[index];
                    if (script.started && !script.faulted && !script.dead) {
                        lua_pushnumber(state.Get(), deltaTime);
                        CallEntityHook(state, index, "on_update", 1);
                    }
                }
            }
        }
        DropDeadEntityScripts();
    }

    void EntityScriptManager::FixedUpdate(lua::State& state, Scene* scene, bool isPlaying, bool scriptsEnabled, float fixedDeltaTime)
    {
        if (!scene || !scriptsEnabled || !isPlaying)
            return;
        ELM_PROFILE_SCOPE_N("Entity Scripts FixedUpdate");
        {
            PassScope pass(m_entityPassDepth);
            ReapEntityScripts(state, scene);
            const size_t count = m_entityScripts.size();
            for (size_t index = 0; index < count; ++index) {
                const auto& script = m_entityScripts[index];
                if (script.started && !script.faulted && !script.dead) {
                    lua_pushnumber(state.Get(), fixedDeltaTime);
                    CallEntityHook(state, index, "on_fixed_update", 1);
                }
            }
        }
        DropDeadEntityScripts();
    }

    void EntityScriptManager::ReapEntityScripts(lua::State& state, Scene* scene)
    {
        for (size_t index = 0; index < m_entityScripts.size(); ++index) {
            const auto& script = m_entityScripts[index];
            if (script.dead)
                continue;
            const auto* components = scene->FindComponents<ScriptComponent>();
            const ScriptComponent* component = components ? components->Find(script.node) : nullptr;
            if (!component || component->module != script.module || scene->IndexOf(script.node) == Scene::NoNode)
                EndEntityScript(state, index, true);
        }
    }

    void EntityScriptManager::StartEntityScripts(lua::State& state, Scene* scene, ScriptModuleManager& modules)
    {
        lua_State* L = state.Get();
        if (const auto* components = scene->FindComponents<ScriptComponent>()) {
            Vector<NodeId> missing;
            for (const NodeId node : components->Ids()) {
                if (FindEntityScriptIndex(node) == NoIndex && scene->IndexOf(node) != Scene::NoNode)
                    missing.push_back(node);
            }
            for (const NodeId node : missing) {
                if (PushEntityScript(state, scene, modules, node))
                    lua_pop(L, 1);
            }
        }

        const size_t count = m_entityScripts.size();
        for (size_t index = 0; index < count; ++index) {
            auto& script = m_entityScripts[index];
            if (script.started || script.faulted || script.dead)
                continue;
            script.started = true;
            CallEntityHook(state, index, "on_start");
        }
    }

    auto EntityScriptManager::PushEntityScript(lua::State& state, Scene* scene, ScriptModuleManager& modules, NodeId node, bool keepFailure) -> EngineResult<void>
    {
        lua_State* L = state.Get();
        auto findComponent = [scene, node]() -> const ScriptComponent* {
            const auto* components = scene ? scene->FindComponents<ScriptComponent>() : nullptr;
            return components ? components->Find(node) : nullptr;
        };

        const ScriptComponent* component = findComponent();
        if (!component) {
            lua_pushnil(L);
            return { };
        }
        if (const size_t index = FindEntityScriptIndex(node); index != NoIndex) {
            const auto& script = m_entityScripts[index];
            if (script.module == component->module) {
                if (!script.self)
                    return MakeUnexpected(EngineError(ErrorCode::ScriptError, script.error));
                script.self.Push(L);
                return { };
            }
            EndEntityScript(state, index, true);
            component = findComponent();
            if (!component) {
                lua_pushnil(L);
                return { };
            }
        }

        const ScriptComponent copy = *component;
        auto made = MakeEntityScript(state, scene, modules, node, copy);
        if (!made && !keepFailure)
            return made;

        EntityScript script;
        script.node = node;
        script.module = copy.module;
        script.serial = m_nextEntitySerial++;
        if (made)
            script.self = lua::Ref(L, -1);
        m_entityScriptIndex[node.Raw()] = static_cast<uint32_t>(m_entityScripts.size());
        m_entityScripts.push_back(std::move(script));
        if (!made)
            FaultEntityScript(m_entityScripts.size() - 1, m_entityScripts.back().serial, "creating the instance", made.error().message);
        return made;
    }

    auto EntityScriptManager::MakeEntityScript(lua::State& state, Scene* /*scene*/, ScriptModuleManager& modules, NodeId node, const ScriptComponent& component) -> EngineResult<void>
    {
        lua_State* L = state.Get();
        const int top = lua_gettop(L);
        if (auto required = modules.Require(state, component.module); !required)
            return required;

        lua_getfield(L, LUA_REGISTRYINDEX, NewInstanceKey);
        lua_insert(L, -2);
        lua::Push(L, bindings::Entity { node });
        lua_createtable(L, 0, static_cast<int>(component.props.size()));
        for (const auto& [name, value] : component.props) {
            bindings::PushScriptValue(L, value);
            lua_setfield(L, -2, name.c_str());
        }
        lua_pushcfunction(L, &bindings::ScriptValueKindOf);
        lua::Push(L, component.module);
        if (auto made = state.ProtectedCall(5, 1); !made) {
            lua_settop(L, top);
            return made;
        }
        return { };
    }

    void EntityScriptManager::CallEntityHook(lua::State& state, size_t index, const char* hook, int argumentCount)
    {
        PassScope pass(m_entityPassDepth);
        lua_State* L = state.Get();
        const uint64_t serial = m_entityScripts[index].serial;

        const int firstArgument = lua_gettop(L) - argumentCount + 1;
        lua_getfield(L, LUA_REGISTRYINDEX, CallHookKey);
        m_entityScripts[index].self.Push(L);
        lua_pushstring(L, hook);
        lua_rotate(L, firstArgument, 3);
        if (auto called = state.ProtectedCall(argumentCount + 2, 0); !called)
            FaultEntityScript(index, serial, hook, called.error().message);
    }

    void EntityScriptManager::DispatchCollisionEvents(lua::State& state, Scene* /*scene*/, std::span<const CollisionEvent> events)
    {
        if (events.empty() || m_entityScripts.empty())
            return;
        ELM_PROFILE_SCOPE_N("Entity Scripts Collisions");
        lua_State* L = state.Get();
        {
            PassScope pass(m_entityPassDepth);
            for (const auto& event : events) {
                const bool enter = event.kind == CollisionEvent::Kind::Enter;
                const char* hook = event.trigger ? (enter ? "on_trigger_enter" : "on_trigger_exit")
                                                 : (enter ? "on_collision_enter" : "on_collision_exit");
                for (int side = 0; side < 2; ++side) {
                    const NodeId self = side == 0 ? event.first : event.second;
                    const NodeId other = side == 0 ? event.second : event.first;
                    const size_t index = FindEntityScriptIndex(self);
                    if (index == NoIndex || !m_entityScripts[index].started || m_entityScripts[index].faulted)
                        continue;

                    lua::Push(L, bindings::Entity { other });
                    int argumentCount = 1;
                    if (enter && !event.trigger) {
                        lua_createtable(L, 0, 2);
                        lua::Push(L, event.point);
                        lua_setfield(L, -2, "point");
                        lua::Push(L, side == 0 ? event.normal : event.normal * -1.0f);
                        lua_setfield(L, -2, "normal");
                        argumentCount = 2;
                    }
                    CallEntityHook(state, index, hook, argumentCount);
                }
            }
        }
        DropDeadEntityScripts();
    }

    void EntityScriptManager::FaultEntityScript(size_t index, uint64_t serial, const char* hook, const String& message)
    {
        auto& script = m_entityScripts[index];
        if (script.serial != serial)
            return;
        script.faulted = true;
        script.error = String(hook) + ": " + message;

        if (!m_reportedEntityErrors.insert(script.module + '\n' + script.error).second)
            return;
        ERROR_MESSAGE(log::Category::Script, LogSource, "Script '%s' of entity #%u failed in %s, the instance is stopped until a module reloads:\n%s",
            script.module.c_str(), script.node.Raw(), hook, message.c_str());
    }

    void EntityScriptManager::EndEntityScript(lua::State& state, size_t index, bool callOnDestroy)
    {
        PassScope pass(m_entityPassDepth);
        auto& script = m_entityScripts[index];
        if (script.dead)
            return;
        script.dead = true;
        m_hasDeadEntityScripts = true;
        if (callOnDestroy && script.started && !script.faulted && script.self)
            CallEntityHook(state, index, "on_destroy");
        m_entityScripts[index].self.Reset();
    }

    size_t EntityScriptManager::FindEntityScriptIndex(NodeId node) const noexcept
    {
        const auto found = m_entityScriptIndex.find(node.Raw());
        if (found == m_entityScriptIndex.end() || m_entityScripts[found->second].dead)
            return NoIndex;
        return found->second;
    }

    auto EntityScriptManager::FindEntityScript(NodeId node) const noexcept -> const EntityScript*
    {
        const size_t index = FindEntityScriptIndex(node);
        return index == NoIndex ? nullptr : &m_entityScripts[index];
    }

    void EntityScriptManager::DestroyEntityScripts(lua::State& state, Scene* /*scene*/, std::span<const NodeId> nodes)
    {
        {
            PassScope pass(m_entityPassDepth);
            for (const NodeId node : nodes) {
                if (const size_t index = FindEntityScriptIndex(node); index != NoIndex)
                    EndEntityScript(state, index, true);
            }
        }
        DropDeadEntityScripts();
    }

    void EntityScriptManager::DestroyAllEntityScripts(lua::State& state, Scene* /*scene*/)
    {
        {
            PassScope pass(m_entityPassDepth);
            const size_t count = m_entityScripts.size();
            for (size_t index = 0; index < count; ++index)
                EndEntityScript(state, index, true);
        }
        DropDeadEntityScripts();
    }

    void EntityScriptManager::DropDeadEntityScripts()
    {
        if (m_entityPassDepth > 0 || !m_hasDeadEntityScripts)
            return;
        m_hasDeadEntityScripts = false;
        std::erase_if(m_entityScripts, [](const EntityScript& script) { return script.dead; });
        m_entityScriptIndex.clear();
        for (size_t index = 0; index < m_entityScripts.size(); ++index)
            m_entityScriptIndex[m_entityScripts[index].node.Raw()] = static_cast<uint32_t>(index);
    }

    void EntityScriptManager::OnModuleReloaded(lua::State& state, StringView name)
    {
        m_reportedEntityErrors.clear();
        {
            PassScope pass(m_entityPassDepth);
            const size_t count = m_entityScripts.size();
            for (size_t index = 0; index < count; ++index) {
                auto& script = m_entityScripts[index];
                if (script.dead || !script.faulted)
                    continue;
                if (script.self) {
                    script.faulted = false;
                    script.error.clear();
                } else {
                    script.dead = true;
                    m_hasDeadEntityScripts = true;
                }
            }
            for (size_t index = 0; index < count; ++index) {
                const auto& script = m_entityScripts[index];
                if (script.started && !script.faulted && !script.dead && script.module == name)
                    CallEntityHook(state, index, "on_script_reload");
            }
        }
        DropDeadEntityScripts();
    }

    void EntityScriptManager::SetEntityScriptField(lua::State& state, NodeId node, StringView name, const ScriptValue& value)
    {
        const size_t index = FindEntityScriptIndex(node);
        if (index == NoIndex || !m_entityScripts[index].self)
            return;
        lua_State* L = state.Get();
        lua::StackGuard guard(L);
        m_entityScripts[index].self.Push(L);
        lua_pushlstring(L, name.data(), name.size());
        bindings::PushScriptValue(L, value);
        lua_rawset(L, -3);
        lua_pop(L, 1);
    }

    void EntityScriptManager::ReleaseEntityScripts() noexcept
    {
        m_entityScripts.clear();
        m_entityScriptIndex.clear();
        m_hasDeadEntityScripts = false;
        m_reportedEntityErrors.clear();
    }

} // namespace elm::script
