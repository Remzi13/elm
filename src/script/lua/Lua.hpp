#pragma once

// Lua is compiled as C++ (third_party/CMakeLists.txt): the C headers are included directly, never lua.hpp,
// whose extern "C" does not match the C++ linkage of the library. As C++, lua_error throws an exception,
// so a Lua error raised inside a binding unwinds its C++ frames and runs their destructors.
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
