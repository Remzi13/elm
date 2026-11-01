#pragma once

#include "core/Std.hpp"

namespace elm {
    class IImGuiWindow;
    class Settings;
}

namespace elm::script {

    class ScriptSystem;

    /// The Scripts window, wired to the parts of the script system (editor/script, editor only).
    /// After ScriptSystem::Init: it takes the Play requests of the engine context and gives the other
    /// windows PlayControls and ScriptInspection. The windows must not outlive the system.
    [[nodiscard]] Vector<UniquePtr<IImGuiWindow>> CreateEditorWindows(ScriptSystem& scripts, Settings& settings);

} // namespace elm::script
