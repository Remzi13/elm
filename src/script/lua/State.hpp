#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"

#include "script/lua/ApiRegistry.hpp"
#include "script/lua/Lua.hpp"

namespace elm::script::lua {

    /// Owns a lua_State. Memory goes through the engine allocator, the State itself is reachable
    /// from any lua_State of it (coroutines included) through State::From.
    /// The object outlives its Lua state: Reset closes it and opens an empty one, so whoever holds
    /// the State keeps the reference across a restart of the scripts.
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
        [[nodiscard]] bool IsOpen() const noexcept { return m_state != nullptr; }

        [[nodiscard]] static State& From(lua_State* L) noexcept;

        /// Closes the Lua state and opens an empty one: no libraries, API description or contexts.
        /// Every Ref into the old state must be released before.
        void Reset();
        /// Closes the Lua state, Reset opens a new one. Every Ref into it must be released before.
        void Close() noexcept;

        /// Context of one kind of bindings: every binding module keeps its own type, so it reaches
        /// only the systems it was registered with. Kept until Reset or Close.
        template <typename T>
        void SetContext(T context)
        {
            SharedPtr<void> value = MakeShared<T>(std::move(context));
            for (auto& slot : m_contexts) {
                if (slot.key == &ContextKey<T>) {
                    slot.value = std::move(value);
                    return;
                }
            }
            m_contexts.push_back({ &ContextKey<T>, std::move(value) });
        }

        /// The context of type T, nullptr when it was not set.
        template <typename T>
        [[nodiscard]] static T* Context(lua_State* L) noexcept
        {
            for (const auto& slot : From(L).m_contexts) {
                if (slot.key == &ContextKey<T>)
                    return static_cast<T*>(slot.value.get());
            }
            return nullptr;
        }

        /// Description of everything registered through Module and Class, for the LuaLS meta file.
        [[nodiscard]] ApiRegistry& Api() noexcept { return m_api; }
        [[nodiscard]] const ApiRegistry& Api() const noexcept { return m_api; }

        /// Opens the libraries scripts may use: no io, os, native modules or file loading
        /// that bypasses the engine file system. debug is opened in debug builds only.
        void OpenLibraries();

        /// Compiles a text chunk and pushes it as a function. chunkName shows up in errors ("@path" for files).
        [[nodiscard]] auto Load(StringView code, const char* chunkName) -> EngineResult<void>;
        /// lua_pcall with a traceback in the error message. Pops the function and arguments, pushes nresults.
        [[nodiscard]] auto ProtectedCall(int argumentCount, int resultCount) -> EngineResult<void>;
        /// Load + ProtectedCall, results are dropped.
        [[nodiscard]] auto Run(StringView code, const char* chunkName) -> EngineResult<void>;
        /// Runs a chunk of the engine's own Lua and keeps its result in the registry under key:
        /// the helpers the script parts call from C++ (lua_getfield(L, LUA_REGISTRYINDEX, key)).
        [[nodiscard]] auto InstallHelper(const char* key, StringView source) -> EngineResult<void>;

        /// Bytes allocated by Lua.
        [[nodiscard]] size_t MemoryUsage() const noexcept;

        /// Message handler for lua_pcall: appends a traceback to the error message.
        static int Traceback(lua_State* L);

    private:
        struct ContextSlot {
            const void* key { nullptr };
            SharedPtr<void> value;
        };

        /// One address per context type, the key of its slot.
        template <typename T>
        static constexpr char ContextKey = 0;

        void Open();
        static void* Allocate(void* userData, void* block, size_t oldSize, size_t newSize) noexcept;
        static int Panic(lua_State* L);

        lua_State* m_state { nullptr };
        Vector<ContextSlot> m_contexts;
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
