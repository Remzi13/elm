#pragma once

#include "core/Std.hpp"

#include "editor/ui/EditorServices.hpp"

namespace elm {
    class Settings;
}

namespace elm::script {

    class ScriptSystem;
    class ScriptWindow;

    /// The Scripts window, wired to the parts of the script system (editor/script, editor only). After
    /// ScriptSystem::Init; the window must not outlive the system.
    [[nodiscard]] UniquePtr<ScriptWindow> CreateScriptWindow(ScriptSystem& scripts, Settings& settings);
    /// What Scene Hierarchy shows and changes of the scripts of a node, from the parts of the script system.
    /// Valid while the system lives.
    [[nodiscard]] ScriptInspection MakeScriptInspection(ScriptSystem& scripts);

} // namespace elm::script
