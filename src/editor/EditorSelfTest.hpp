#pragma once

namespace elm::editor {

    /// Tests of the editor's own logic without a window: syntax coloring, indentation and error
    /// locations of the script editor, Edit mode, the edit history, Scene Hierarchy on ImGui without a
    /// window system. Prints a report to stdout and returns the number of failures.
    /// Started by `editor --editor-selftest`.
    [[nodiscard]] int RunSelfTests();

} // namespace elm::editor
