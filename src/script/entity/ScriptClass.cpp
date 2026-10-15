#include "script/entity/ScriptClass.hpp"

#include "script/lua/State.hpp"
#include "script/module/ModuleLoader.hpp"
#include "script/values/Entity.hpp"

#include "core/Debug.hpp"
#include "core/Unexpected.hpp"

#include <algorithm>

namespace elm::script {

    namespace {

        /// Registry key of the class helper.
        constexpr const char* HelperKey = "elm.script_class";

        /// { props = props(class, module), new = new(class, entity, overrides, kind_of, module) }
        ///  - props: class.props (inherited ones too), nil without props; an error when the module is no class.
        ///  - new: self with the defaults of class.props (vectors copied), the overrides on top,
        ///    self.entity and the class as __index. kind_of(value) tells the prop type of a value,
        ///    nil for a type props cannot hold.
        constexpr const char* HelperSource = R"lua(
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

            local function props(class, module)
                if type(class) ~= "table" then
                    error(("module '%s' returns %s: an entity script returns its class table"):format(module, type(class)), 0)
                end
                local defaults = class.props
                if defaults ~= nil and type(defaults) ~= "table" then
                    error(module .. ".props must be a table", 0)
                end
                return defaults
            end

            local function new(class, entity, overrides, kind_of, module)
                local defaults = props(class, module)
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

            return { props = props, new = new }
        )lua";

        /// The props table at index as prop values, sorted by name.
        auto ToProps(lua_State* L, int table, StringView module) -> EngineResult<ScriptProps>
        {
            ScriptProps props;
            if (!lua_istable(L, table))
                return props;
            // Raw: the props table is plain data, a metamethod could fail outside a protected call
            lua_pushnil(L);
            while (lua_next(L, table)) {
                String key = lua_type(L, -2) == LUA_TSTRING ? String(lua_tostring(L, -2)) : String();
                auto value = ToScriptValue(L, -1);
                lua_pop(L, 1);
                if (key.empty() || key == "entity" || !value) {
                    lua_pop(L, 1);
                    return MakeUnexpected(EngineError(ErrorCode::ScriptError,
                        String(module) + ".props has a field that cannot be a prop" + (key.empty() ? String() : ": " + key)));
                }
                props.emplace_back(std::move(key), std::move(*value));
            }
            std::sort(props.begin(), props.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
            return props;
        }

    } // namespace

    ScriptClass::ScriptClass(lua::State& state, ModuleLoader& loader) noexcept
        : m_state(state)
        , m_loader(loader)
    {
    }

    void ScriptClass::Init()
    {
        const bool installed = m_state.InstallHelper(HelperKey, HelperSource).has_value();
        ELM_ASSERT(installed && "the class helper is engine code and must load");
    }

    auto ScriptClass::PushInstance(StringView module, NodeId entity, const ScriptProps& overrides) -> EngineResult<void>
    {
        lua_State* L = m_state;
        const int top = lua_gettop(L);
        if (auto required = m_loader.Require(module); !required)
            return required;

        // new(class, entity, overrides, kind_of, module)
        PushHelper("new");
        lua_insert(L, -2);
        lua::Push(L, Entity { entity });
        lua_createtable(L, 0, static_cast<int>(overrides.size()));
        for (const auto& [name, value] : overrides) {
            PushScriptValue(L, value);
            lua_setfield(L, -2, name.c_str());
        }
        lua_pushcfunction(L, &ScriptValueKindOf);
        lua::Push(L, module);
        if (auto made = m_state.ProtectedCall(5, 1); !made) {
            lua_settop(L, top);
            return made;
        }
        return { };
    }

    auto ScriptClass::ReadDefaults(StringView module) -> EngineResult<ScriptProps>
    {
        if (!m_state.IsOpen())
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "scripts are not running"));
        lua_State* L = m_state;
        lua::StackGuard guard(L);
        if (auto required = m_loader.Require(module); !required)
            return MakeUnexpected(required.error());

        // props(class, module)
        PushHelper("props");
        lua_insert(L, -2);
        lua::Push(L, module);
        if (auto called = m_state.ProtectedCall(2, 1); !called)
            return MakeUnexpected(called.error());
        auto props = ToProps(L, lua_gettop(L), module);
        lua_pop(L, 1);
        return props;
    }

    void ScriptClass::PushHelper(const char* function)
    {
        lua_State* L = m_state;
        lua_getfield(L, LUA_REGISTRYINDEX, HelperKey);
        lua_getfield(L, -1, function);
        lua_remove(L, -2);
    }

} // namespace elm::script
