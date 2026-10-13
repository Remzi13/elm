#pragma once

// Engine bindings for Lua, on top of the plain C API:
//   State       - the lua_State, engine allocator, protected calls with tracebacks
//   StackTraits - C++ <-> Lua value conversion
//   Module      - function tables (elm.math, elm.log, ...)
//   Class<T>    - C++ value types as userdata with fields, methods and metamethods
//   Ref         - C++ references to Lua values, FunctionRef to call back into Lua
#include "script/lua/Class.hpp"
#include "script/lua/Module.hpp"
#include "script/lua/Ref.hpp"
#include "script/lua/Stack.hpp"
#include "script/lua/State.hpp"
