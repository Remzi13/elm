#pragma once

#include "core/FileSystem.hpp"

namespace elm::script {

    /// Tests of the Lua bindings: C++ checks plus every assets/scripts/tests/*_test.lua file,
    /// run against test classes and functions registered as module elm_test.
    /// Prints a report to stdout and returns the number of failures. Started by `Engine --script-selftest`.
    [[nodiscard]] int RunSelfTests(const Path& scriptRoot);

} // namespace elm::script
