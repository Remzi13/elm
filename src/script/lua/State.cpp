#include "script/lua/State.hpp"

#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Memory.hpp"
#include "core/Unexpected.hpp"

#include <cstring>

namespace elm::script::lua {

    static_assert(LUA_EXTRASPACE >= sizeof(State*), "the State pointer lives in the extra space of lua_State");

    namespace {

        auto ScriptError(const char* message) -> std::unexpected<EngineError>
        {
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, message ? message : "unknown Lua error"));
        }

        /// Error object at the top of the stack as text; it is popped.
        auto PopError(lua_State* L) -> std::unexpected<EngineError>
        {
            const char* message = lua_tostring(L, -1);
            auto error = ScriptError(message ? message : luaL_typename(L, -1));
            lua_pop(L, 1);
            return error;
        }

    } // namespace

    State::State()
    {
        m_state = lua_newstate(&State::Allocate, nullptr);
        ELM_ASSERT(m_state != nullptr);
        *static_cast<State**>(lua_getextraspace(m_state)) = this;
        lua_atpanic(m_state, &State::Panic);
        // Frame-sized pauses: scripts allocate small temporaries every frame
        lua_gc(m_state, LUA_GCGEN, 0, 0);
    }

    State::~State()
    {
        if (m_state)
            lua_close(m_state);
    }

    State& State::From(lua_State* L) noexcept
    {
        return **static_cast<State**>(lua_getextraspace(L));
    }

    void State::OpenLibraries()
    {
        StackGuard guard(m_state);

        static constexpr luaL_Reg libraries[] = {
            { LUA_GNAME, luaopen_base },
            { LUA_LOADLIBNAME, luaopen_package },
            { LUA_COLIBNAME, luaopen_coroutine },
            { LUA_TABLIBNAME, luaopen_table },
            { LUA_STRLIBNAME, luaopen_string },
            { LUA_MATHLIBNAME, luaopen_math },
            { LUA_UTF8LIBNAME, luaopen_utf8 },
#if !defined(NDEBUG)
            { LUA_DBLIBNAME, luaopen_debug },
#endif
        };
        for (const auto& library : libraries) {
            luaL_requiref(m_state, library.name, library.func, 1);
            lua_pop(m_state, 1);
        }

        // Files are read by the engine (require goes through its searcher), never by path from a script
        lua_pushnil(m_state);
        lua_setglobal(m_state, "dofile");
        lua_pushnil(m_state);
        lua_setglobal(m_state, "loadfile");

        lua_getglobal(m_state, LUA_LOADLIBNAME);
        for (const char* field : { "loadlib", "cpath", "searchpath" }) {
            lua_pushnil(m_state);
            lua_setfield(m_state, -2, field);
        }
        lua_pop(m_state, 1);

        // load() accepts text only, like State::Load: unverified bytecode can crash the VM.
        // An explicit nil env must stay distinct from no env, hence the vararg.
        const auto restricted = Run(R"(
            local base_load = load
            load = function(chunk, chunkname, _, ...)
                if select("#", ...) > 0 then
                    return base_load(chunk, chunkname, "t", (...))
                end
                return base_load(chunk, chunkname, "t")
            end
        )", "=engine");
        ELM_ASSERT(restricted.has_value());
    }

    auto State::Load(StringView code, const char* chunkName) -> EngineResult<void>
    {
        // Text only: precompiled bytecode is not verified by Lua and could crash the engine
        if (luaL_loadbufferx(m_state, code.data(), code.size(), chunkName, "t") != LUA_OK)
            return PopError(m_state);
        return { };
    }

    auto State::ProtectedCall(int argumentCount, int resultCount) -> EngineResult<void>
    {
        const int handlerIndex = lua_gettop(m_state) - argumentCount;
        lua_pushcfunction(m_state, &State::Traceback);
        lua_insert(m_state, handlerIndex);
        const int status = lua_pcall(m_state, argumentCount, resultCount, handlerIndex);
        lua_remove(m_state, handlerIndex);
        if (status != LUA_OK)
            return PopError(m_state);
        return { };
    }

    auto State::Run(StringView code, const char* chunkName) -> EngineResult<void>
    {
        if (auto loaded = Load(code, chunkName); !loaded)
            return loaded;
        return ProtectedCall(0, 0);
    }

    size_t State::MemoryUsage() const noexcept
    {
        return static_cast<size_t>(lua_gc(m_state, LUA_GCCOUNT, 0)) * 1024
            + static_cast<size_t>(lua_gc(m_state, LUA_GCCOUNTB, 0));
    }

    int State::Traceback(lua_State* L)
    {
        const char* message = lua_tostring(L, 1);
        if (!message) {
            // An error object that is not a string: use its __tostring when it has one
            if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
                return 1;
            message = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
        }
        luaL_traceback(L, L, message, 1);
        return 1;
    }

    void* State::Allocate(void*, void* block, size_t oldSize, size_t newSize) noexcept
    {
        if (newSize == 0) {
            if (block)
                memory::deallocate_impl(block, oldSize);
            return nullptr;
        }
        if (!block) {
            // oldSize holds the type of the new object here, not a size
            try {
                return memory::allocate_impl(newSize);
            } catch (const std::bad_alloc&) {
                return nullptr;
            }
        }
        return memory::reallocate_impl(block, oldSize, newSize);
    }

    int State::Panic(lua_State* L)
    {
        const char* message = lua_tostring(L, -1);
        FATAL_ERROR_MESSAGE(log::Category::Script, "lua", "Unprotected Lua error: %s", message ? message : "?");
        ELM_DEBUG_BREAK();
        return 0; // Lua aborts after the panic handler returns
    }

    StackGuard::~StackGuard()
    {
#if !defined(NDEBUG)
        if (lua_gettop(m_state) != m_top) {
            ERROR_MESSAGE(log::Category::Script, "lua", "Lua stack is unbalanced: %d values on entry, %d on exit",
                m_top, lua_gettop(m_state));
            ELM_ASSERT(false);
        }
#endif
    }

} // namespace elm::script::lua
