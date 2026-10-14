#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"
#include "script/lua/Bind.hpp"

namespace elm::script {

    class ScriptConsole {
    public:
        ScriptConsole() = default;
        ~ScriptConsole() = default;

        ScriptConsole(const ScriptConsole&) = delete;
        ScriptConsole& operator=(const ScriptConsole&) = delete;

        void InstallHelpers(lua::State& state);

        [[nodiscard]] auto Execute(lua::State& state, StringView code) -> EngineResult<String>;
        [[nodiscard]] Vector<String> Complete(lua::State& state, StringView text);

        [[nodiscard]] String* Capture() noexcept { return m_capture; }
        void SetCapture(String* capture) noexcept { m_capture = capture; }

    private:
        String* m_capture { nullptr };
    };

} // namespace elm::script
