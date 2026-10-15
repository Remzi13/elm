#pragma once

#include "core/Error.hpp"

#include "script/lua/Lua.hpp"

namespace elm::script {

    namespace lua {
        class State;
    }

    /// The Lua console: runs a line of Lua in the live state and completes names. The global print
    /// goes through the console, so the output of a command comes back with its result; outside a
    /// command print only logs, as before.
    /// Owner: ScriptSystem, thread: update. Before Init it answers that the scripts are not initialized.
    class ScriptConsole {
    public:
        explicit ScriptConsole(lua::State& state) noexcept;

        ScriptConsole(const ScriptConsole&) = delete;
        ScriptConsole& operator=(const ScriptConsole&) = delete;
        ScriptConsole(ScriptConsole&&) = delete;
        ScriptConsole& operator=(ScriptConsole&&) = delete;

        /// Installs the result formatter and wraps the global print; after the bindings registered it.
        void Init();

        /// Runs a line of Lua. An expression gives its values as text, a statement just runs;
        /// print() output of the command is included.
        [[nodiscard]] auto Execute(StringView code) -> EngineResult<String>;
        /// Completions of the last name in text ("elm.sc" -> "elm.scene"), each as the whole new text.
        /// Looks through tables and class userdata with raw access only, so it never runs script code.
        [[nodiscard]] Vector<String> Complete(StringView text);

    private:
        /// print(...): the line goes to the output of the running command, then to the print it wraps.
        static int Print(lua_State* L);
        /// Runs the compiled command at the top of the stack and appends its results to output.
        [[nodiscard]] auto Run(String& output) -> EngineResult<void>;

        lua::State& m_state;
        /// Output of the command that runs, nullptr between commands.
        String* m_output { nullptr };
    };

} // namespace elm::script
