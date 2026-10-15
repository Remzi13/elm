#pragma once

#include "script/ui/ScriptWindowActions.hpp"

#include <array>

namespace elm::script {

    namespace lua {
        class State;
    }

    /// The API tab of the Scripts window: everything the bindings registered in the Lua state,
    /// with types and a filter. A click hands the call text over (into the console).
    /// Owner: ScriptWindow, thread: update (UI). Empty while the state is closed.
    class ApiPanel {
    public:
        using InsertText = std::function<void(StringView text)>;

        ApiPanel(const lua::State& state, InsertText insert);

        void Draw();

    private:
        const lua::State& m_state;
        InsertText m_insert;
        std::array<char, 128> m_filter { };
    };

} // namespace elm::script
