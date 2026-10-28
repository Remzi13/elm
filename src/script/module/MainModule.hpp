#pragma once

#include "core/Error.hpp"

#include "script/lua/Ref.hpp"

namespace elm::script {

    class ModuleLoader;

    enum class MainState : uint8_t { Idle, Running, Faulted };

    /// The main module (entry.mainModule of the project): a table of hooks the engine calls,
    ///     on_start(), on_update(dt), on_fixed_update(dt), on_play(), on_stop(), on_shutdown(),
    /// and of functions the editor calls by name (build_scene). A hook that fails stops the hooks
    /// until the module is reloaded or the scripts restart.
    /// Owner: ScriptSystem, thread: update. While Idle (no module started) the calls do nothing.
    class MainModule {
    public:
        /// Builds the scene ("Rebuild scene" in the Scripts window).
        static constexpr const char* BuildSceneFunction = "build_scene";

        MainModule(lua::State& state, ModuleLoader& loader) noexcept;

        MainModule(const MainModule&) = delete;
        MainModule& operator=(const MainModule&) = delete;
        MainModule(MainModule&&) = delete;
        MainModule& operator=(MainModule&&) = delete;

        /// Requires the module and calls its on_start. A missing file is not an error: the module stays Idle.
        [[nodiscard]] auto Start(StringView name) -> EngineResult<void>;
        /// Forgets the module (after on_shutdown): its Lua state is going away.
        void Shutdown() noexcept;

        void CallHook(const char* hook);
        void CallHook(const char* hook, float deltaTime);
        /// Calls main.<function>() in protected mode; false when the module has no such function.
        /// A failure is returned, it does not stop the hooks.
        [[nodiscard]] auto CallFunction(const char* function) -> EngineResult<bool>;

        /// A module was reloaded. For the main module: its fault is cleared, and a main module that
        /// failed to load until now starts with this version.
        void OnModuleReloaded(StringView name);

        [[nodiscard]] MainState GetState() const noexcept;
        [[nodiscard]] const String& Name() const noexcept { return m_name; }
        /// Why the hooks stopped, empty unless Faulted.
        [[nodiscard]] const String& Error() const noexcept { return m_error; }

    private:
        /// Pushes the hook when the module runs and has it; false, nothing pushed, otherwise.
        [[nodiscard]] bool PushHook(const char* hook);
        /// Pushes main.<name> when it is a function; false, nothing pushed, otherwise.
        [[nodiscard]] bool PushFunction(const char* name);
        /// Calls the pushed hook with its arguments; a failure stops the hooks.
        void RunHook(const char* hook, int argumentCount);

        lua::State& m_state;
        ModuleLoader& m_loader;
        lua::Ref m_module;
        String m_name;
        String m_error;
        bool m_faulted { false };
    };

} // namespace elm::script
