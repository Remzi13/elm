#include "script/ScriptSystem.hpp"

#include "script/bindings/Entity.hpp"
#include "script/bindings/ScriptValues.hpp"

#include "Scene/PhysicsComponents.hpp"
#include "Scene/ScriptComponent.hpp"

#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"
#include "core/Unexpected.hpp"

#include <algorithm>

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "ScriptSystem";
        constexpr size_t NoIndex = ~size_t { 0 };

        // Registry keys of the Lua helpers installed by InstallEntityScriptHelpers
        constexpr const char* NewInstanceKey = "elm.entity_script_new";
        constexpr const char* CallHookKey = "elm.entity_script_call";
        constexpr const char* PropsKey = "elm.entity_script_props";

        /// props(class, module) -> class.props (inherited ones too), nil without props.
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

        /// new(class, entity, overrides, kind_of, module) -> self: the defaults of class.props (vectors
        /// copied), the overrides on top, self.entity, the class as __index. kind_of(value) tells the
        /// prop type of a value, nil for a type props cannot hold.
        constexpr const char* NewInstanceSource = R"lua(
            local metas = setmetatable({}, { __mode = "k" })

            -- One metatable per class: instances find their methods, inherited ones too, in the class
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
                    -- Vectors are mutable objects: every instance gets its own
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

        /// call(self, name, ...): self:name(...) when the instance or its class has such a function.
        constexpr const char* CallHookSource = R"lua(
            return function(self, name, ...)
                local hook = self[name]
                if hook ~= nil then return hook(self, ...) end
            end
        )lua";

        /// Counts a running pass over the instances: dead ones keep their place until it ends.
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

    void ScriptSystem::SetEngineContext(const EngineContext& context) noexcept
    {
        // Instances and the Play snapshot belong to their scene
        if (context.scene != m_engine.scene) {
            ReleaseEntityScripts();
            m_snapshot.reset();
        }
        m_engine = context;
    }

    void ScriptSystem::InstallEntityScriptHelpers()
    {
        lua_State* L = *m_state;
        for (const auto& [key, source] : { std::pair { NewInstanceKey, NewInstanceSource }, std::pair { CallHookKey, CallHookSource },
                 std::pair { PropsKey, PropsSource } }) {
            const bool installed = m_state->Load(source, "=engine") && m_state->ProtectedCall(0, 1);
            ELM_ASSERT(installed);
            if (installed)
                lua_setfield(L, LUA_REGISTRYINDEX, key);
        }
    }

    // ── Passes ──────────────────────────────────────────────────────────────────

    void ScriptSystem::UpdateEntityScripts(float deltaTime)
    {
        if (!m_state || !m_engine.scene || !m_scriptsEnabled)
            return;
        ELM_PROFILE_SCOPE_N("Entity Scripts Update");
        {
            PassScope pass(m_entityPassDepth);
            ReapEntityScripts();
            // Edit mode: the instances wait for Play to start
            if (m_playing) {
                StartEntityScripts();
                // Instances made by these calls start on the next pass
                const size_t count = m_entityScripts.size();
                for (size_t index = 0; index < count; ++index) {
                    const auto& script = m_entityScripts[index];
                    if (script.started && !script.faulted && !script.dead) {
                        lua_pushnumber(*m_state, deltaTime);
                        CallEntityHook(index, "on_update", 1);
                    }
                }
            }
        }
        DropDeadEntityScripts();
    }

    void ScriptSystem::FixedUpdateEntityScripts(float fixedDeltaTime)
    {
        if (!m_state || !m_engine.scene || !m_scriptsEnabled || !m_playing)
            return;
        ELM_PROFILE_SCOPE_N("Entity Scripts FixedUpdate");
        {
            PassScope pass(m_entityPassDepth);
            ReapEntityScripts();
            const size_t count = m_entityScripts.size();
            for (size_t index = 0; index < count; ++index) {
                const auto& script = m_entityScripts[index];
                if (script.started && !script.faulted && !script.dead) {
                    lua_pushnumber(*m_state, fixedDeltaTime);
                    CallEntityHook(index, "on_fixed_update", 1);
                }
            }
        }
        DropDeadEntityScripts();
    }

    void ScriptSystem::ReapEntityScripts()
    {
        const Scene& scene = *m_engine.scene;
        for (size_t index = 0; index < m_entityScripts.size(); ++index) {
            const auto& script = m_entityScripts[index];
            if (script.dead)
                continue;
            // Looked up again every time: on_destroy calls may change the scene
            const auto* components = scene.FindComponents<ScriptComponent>();
            const ScriptComponent* component = components ? components->Find(script.node) : nullptr;
            if (!component || component->module != script.module || scene.IndexOf(script.node) == Scene::NoNode)
                EndEntityScript(index, true);
        }
    }

    void ScriptSystem::StartEntityScripts()
    {
        lua_State* L = *m_state;
        const Scene& scene = *m_engine.scene;

        // Components added without Lua (scene files, C++) get their instances here
        if (const auto* components = scene.FindComponents<ScriptComponent>()) {
            Vector<NodeId> missing;
            for (const NodeId node : components->Ids()) {
                if (FindEntityScriptIndex(node) == NoIndex && scene.IndexOf(node) != Scene::NoNode)
                    missing.push_back(node);
            }
            for (const NodeId node : missing) {
                // A failure is kept as a faulted instance and reported
                if (PushEntityScript(node))
                    lua_pop(L, 1);
            }
        }

        // Instances made by on_start calls wait for the next pass
        const size_t count = m_entityScripts.size();
        for (size_t index = 0; index < count; ++index) {
            auto& script = m_entityScripts[index];
            if (script.started || script.faulted || script.dead)
                continue;
            // on_destroy follows exactly the instances whose on_start was called
            script.started = true;
            CallEntityHook(index, "on_start");
        }
    }

    // ── Instances ───────────────────────────────────────────────────────────────

    auto ScriptSystem::PushEntityScript(NodeId node, bool keepFailure) -> EngineResult<void>
    {
        lua_State* L = *m_state;
        auto findComponent = [this, node]() -> const ScriptComponent* {
            const auto* components = m_engine.scene ? m_engine.scene->FindComponents<ScriptComponent>() : nullptr;
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
            // The component names another module now
            EndEntityScript(index, true);
            component = findComponent();
            if (!component) {
                lua_pushnil(L);
                return { };
            }
        }

        // Making the instance runs Lua (the module body), which may change the component
        const ScriptComponent copy = *component;
        auto made = MakeEntityScript(node, copy);
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
        // Kept faulted: not made again before a module reloads
        if (!made)
            FaultEntityScript(m_entityScripts.size() - 1, m_entityScripts.back().serial, "creating the instance", made.error().message);
        return made;
    }

    auto ScriptSystem::MakeEntityScript(NodeId node, const ScriptComponent& component) -> EngineResult<void>
    {
        lua_State* L = *m_state;
        const int top = lua_gettop(L);
        if (auto required = Require(component.module); !required)
            return required;

        // new(class, entity, overrides, kind_of, module)
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
        if (auto made = m_state->ProtectedCall(5, 1); !made) {
            lua_settop(L, top);
            return made;
        }
        return { };
    }

    void ScriptSystem::CallEntityHook(size_t index, const char* hook, int argumentCount)
    {
        // Entries keep their index while the call runs
        PassScope pass(m_entityPassDepth);
        lua_State* L = *m_state;
        const uint64_t serial = m_entityScripts[index].serial;

        // call(self, hook, arguments...): the helper, self and the name go below the arguments
        const int firstArgument = lua_gettop(L) - argumentCount + 1;
        lua_getfield(L, LUA_REGISTRYINDEX, CallHookKey);
        m_entityScripts[index].self.Push(L);
        lua_pushstring(L, hook);
        lua_rotate(L, firstArgument, 3);
        if (auto called = m_state->ProtectedCall(argumentCount + 2, 0); !called)
            FaultEntityScript(index, serial, hook, called.error().message);
    }

    void ScriptSystem::DispatchCollisionEvents(std::span<const CollisionEvent> events)
    {
        if (!m_state || !m_scriptsEnabled || !m_playing || events.empty() || m_entityScripts.empty())
            return;
        ELM_PROFILE_SCOPE_N("Entity Scripts Collisions");
        lua_State* L = *m_state;
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
                        // contact = { point, normal }, the normal pointing from this entity to the other one
                        lua_createtable(L, 0, 2);
                        lua::Push(L, event.point);
                        lua_setfield(L, -2, "point");
                        lua::Push(L, side == 0 ? event.normal : event.normal * -1.0f);
                        lua_setfield(L, -2, "normal");
                        argumentCount = 2;
                    }
                    CallEntityHook(index, hook, argumentCount);
                }
            }
        }
        DropDeadEntityScripts();
    }

    void ScriptSystem::FaultEntityScript(size_t index, uint64_t serial, const char* hook, const String& message)
    {
        auto& script = m_entityScripts[index];
        // The instance ended during the call and another took its place
        if (script.serial != serial)
            return;
        script.faulted = true;
        script.error = String(hook) + ": " + message;

        // Instances sharing a bug fail the same way: report it once until a module reloads
        if (!m_reportedEntityErrors.insert(script.module + '\n' + script.error).second)
            return;
        const Scene::Node* node = m_engine.scene ? m_engine.scene->Find(script.node) : nullptr;
        ERROR_MESSAGE(log::Category::Script, LogSource, "Script '%s' of entity '%s' failed in %s, the instance is stopped until a module reloads:\n%s",
            script.module.c_str(), node ? node->name.c_str() : "?", hook, message.c_str());
    }

    void ScriptSystem::EndEntityScript(size_t index, bool callOnDestroy)
    {
        PassScope pass(m_entityPassDepth);
        auto& script = m_entityScripts[index];
        if (script.dead)
            return;
        // Dead first: an on_destroy that destroys its own entity does not end it twice
        script.dead = true;
        m_hasDeadEntityScripts = true;
        if (callOnDestroy && script.started && !script.faulted && script.self)
            CallEntityHook(index, "on_destroy");
        m_entityScripts[index].self.Reset();
    }

    size_t ScriptSystem::FindEntityScriptIndex(NodeId node) const noexcept
    {
        const auto found = m_entityScriptIndex.find(node.Raw());
        if (found == m_entityScriptIndex.end() || m_entityScripts[found->second].dead)
            return NoIndex;
        return found->second;
    }

    auto ScriptSystem::FindEntityScript(NodeId node) const noexcept -> const EntityScript*
    {
        const size_t index = FindEntityScriptIndex(node);
        return index == NoIndex ? nullptr : &m_entityScripts[index];
    }

    void ScriptSystem::DestroyEntityScripts(std::span<const NodeId> nodes)
    {
        if (!m_state)
            return;
        {
            PassScope pass(m_entityPassDepth);
            for (const NodeId node : nodes) {
                if (const size_t index = FindEntityScriptIndex(node); index != NoIndex)
                    EndEntityScript(index, true);
            }
        }
        DropDeadEntityScripts();
    }

    void ScriptSystem::DestroyAllEntityScripts()
    {
        if (!m_state)
            return;
        {
            PassScope pass(m_entityPassDepth);
            // Instances made by on_destroy calls stay: their nodes are checked on the next pass
            const size_t count = m_entityScripts.size();
            for (size_t index = 0; index < count; ++index)
                EndEntityScript(index, true);
        }
        DropDeadEntityScripts();
    }

    void ScriptSystem::DropDeadEntityScripts()
    {
        if (m_entityPassDepth > 0 || !m_hasDeadEntityScripts)
            return;
        m_hasDeadEntityScripts = false;
        std::erase_if(m_entityScripts, [](const EntityScript& script) { return script.dead; });
        m_entityScriptIndex.clear();
        for (size_t index = 0; index < m_entityScripts.size(); ++index)
            m_entityScriptIndex[m_entityScripts[index].node.Raw()] = static_cast<uint32_t>(index);
    }

    void ScriptSystem::OnModuleReloaded(StringView name)
    {
        m_reportedEntityErrors.clear();
        m_scriptDefaults.clear();
        {
            PassScope pass(m_entityPassDepth);
            const size_t count = m_entityScripts.size();
            // Any reload may have fixed what stopped an instance (a module it uses, its own code)
            for (size_t index = 0; index < count; ++index) {
                auto& script = m_entityScripts[index];
                if (script.dead || !script.faulted)
                    continue;
                if (script.self) {
                    script.faulted = false;
                    script.error.clear();
                } else {
                    // The instance could not be made: the next pass makes it again
                    script.dead = true;
                    m_hasDeadEntityScripts = true;
                }
            }
            for (size_t index = 0; index < count; ++index) {
                const auto& script = m_entityScripts[index];
                if (script.started && !script.faulted && !script.dead && script.module == name)
                    CallEntityHook(index, "on_script_reload");
            }
        }
        DropDeadEntityScripts();
    }

    // ── Inspector ───────────────────────────────────────────────────────────────

    auto ScriptSystem::ScriptDefaults(StringView module) -> const EngineResult<ScriptProps>&
    {
        const String name(module);
        if (const auto found = m_scriptDefaults.find(name); found != m_scriptDefaults.end())
            return found->second;

        auto read = [&]() -> EngineResult<ScriptProps> {
            if (!m_state)
                return MakeUnexpected(EngineError(ErrorCode::ScriptError, "scripts are not running"));
            lua_State* L = *m_state;
            lua::StackGuard guard(L);
            if (auto required = Require(name); !required)
                return MakeUnexpected(required.error());
            lua_getfield(L, LUA_REGISTRYINDEX, PropsKey);
            lua_insert(L, -2);
            lua::Push(L, name);
            if (auto called = m_state->ProtectedCall(2, 1); !called)
                return MakeUnexpected(called.error());

            ScriptProps props;
            const int table = lua_gettop(L);
            if (lua_istable(L, table)) {
                // Raw: the props table is plain data, a metamethod could fail outside a protected call
                lua_pushnil(L);
                while (lua_next(L, table)) {
                    String key = lua_type(L, -2) == LUA_TSTRING ? String(lua_tostring(L, -2)) : String();
                    auto value = bindings::ToScriptValue(L, -1);
                    lua_pop(L, 1);
                    if (key.empty() || key == "entity" || !value) {
                        lua_settop(L, table - 1);
                        return MakeUnexpected(EngineError(ErrorCode::ScriptError,
                            name + ".props has a field that cannot be a prop" + (key.empty() ? String() : ": " + key)));
                    }
                    props.emplace_back(std::move(key), std::move(*value));
                }
            }
            lua_settop(L, table - 1);
            std::sort(props.begin(), props.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
            return props;
        };
        return m_scriptDefaults.emplace(name, read()).first->second;
    }

    void ScriptSystem::SetEntityScriptField(NodeId node, StringView name, const ScriptValue& value)
    {
        const size_t index = FindEntityScriptIndex(node);
        if (!m_state || index == NoIndex || !m_entityScripts[index].self)
            return;
        lua_State* L = *m_state;
        lua::StackGuard guard(L);
        m_entityScripts[index].self.Push(L);
        lua_pushlstring(L, name.data(), name.size());
        bindings::PushScriptValue(L, value);
        lua_rawset(L, -3);
        lua_pop(L, 1);
    }

    Vector<String> ScriptSystem::ListModules() const
    {
        Vector<String> modules;
        Vector<Path> folders { m_scriptRoot };
        while (!folders.empty()) {
            const Path folder = std::move(folders.back());
            folders.pop_back();
            std::error_code error;
            for (auto it = Directory(folder).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
                const Path path = it.CurrentPath();
                std::error_code typeError;
                if (it.IsDirectory(typeError)) {
                    if (!path.Filename().string().starts_with("."))
                        folders.push_back(path);
                } else if (String module = ModuleNameOf(path); !module.empty()) {
                    modules.push_back(std::move(module));
                }
            }
        }
        std::sort(modules.begin(), modules.end());
        return modules;
    }

    void ScriptSystem::ReleaseEntityScripts() noexcept
    {
        m_entityScripts.clear();
        m_entityScriptIndex.clear();
        m_hasDeadEntityScripts = false;
        m_reportedEntityErrors.clear();
    }

} // namespace elm::script
