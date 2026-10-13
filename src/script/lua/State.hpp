#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"

#include "script/lua/ApiRegistry.hpp"
#include "script/lua/Lua.hpp"

namespace elm::script::lua {

    /// Owns a lua_State. Memory goes through the engine allocator, the State itself is reachable
    /// from any lua_State of it (coroutines included) through State::From.
    class State {
    public:
        State();
        ~State();

        State(const State&) = delete;
        State& operator=(const State&) = delete;
        State(State&&) = delete;
        State& operator=(State&&) = delete;

        [[nodiscard]] lua_State* Get() const noexcept { return m_state; }
        [[nodiscard]] operator lua_State*() const noexcept { return m_state; }

        [[nodiscard]] static State& From(lua_State* L) noexcept;

        /// Opaque pointer for the bindings (the engine systems they talk to).
        void SetContext(void* context) noexcept { m_context = context; }
        template <typename T>
        [[nodiscard]] static T* Context(lua_State* L) noexcept { return static_cast<T*>(From(L).m_context); }

        /// Description of everything registered through Module and Class, for the LuaLS meta file.
        [[nodiscard]] ApiRegistry& Api() noexcept { return m_api; }

        /// Opens the libraries scripts may use: no io, os, native modules or file loading
        /// that bypasses the engine file system. debug is opened in debug builds only.
        void OpenLibraries();

        /// Compiles a text chunk and pushes it as a function. chunkName shows up in errors ("@path" for files).
        [[nodiscard]] auto Load(StringView code, const char* chunkName) -> EngineResult<void>;
        /// lua_pcall with a traceback in the error message. Pops the function and arguments, pushes nresults.
        [[nodiscard]] auto ProtectedCall(int argumentCount, int resultCount) -> EngineResult<void>;
        /// Load + ProtectedCall, results are dropped.
        [[nodiscard]] auto Run(StringView code, const char* chunkName) -> EngineResult<void>;

        /// Bytes allocated by Lua.
        [[nodiscard]] size_t MemoryUsage() const noexcept;

        /// Message handler for lua_pcall: appends a traceback to the error message.
        static int Traceback(lua_State* L);

    private:
        static void* Allocate(void* userData, void* block, size_t oldSize, size_t newSize) noexcept;
        static int Panic(lua_State* L);

        lua_State* m_state { nullptr };
        void* m_context { nullptr };
        ApiRegistry m_api;
    };

    /// Checks that a scope leaves the Lua stack as it found it (debug builds).
    class StackGuard {
    public:
        explicit StackGuard(lua_State* L) noexcept : m_state(L), m_top(lua_gettop(L)) { }
        ~StackGuard();

        StackGuard(const StackGuard&) = delete;
        StackGuard& operator=(const StackGuard&) = delete;

    private:
        lua_State* m_state;
        int m_top;
    };

} // namespace elm::script::lua
