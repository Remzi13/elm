#pragma once

namespace elm::editor {

    /// Tests of the editor's own logic without a window: syntax coloring, indentation and error
    /// locations of the script editor. Prints a report to stdout and returns the number of failures.
    /// Started by `Engine --editor-selftest`.
    [[nodiscard]] int RunSelfTests();

} // namespace elm::editor
