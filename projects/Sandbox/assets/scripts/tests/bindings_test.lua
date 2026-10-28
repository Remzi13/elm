-- Lua side of the binding layer tests: elm_test is registered by src/script/SelfTest.cpp.
local expect = require "tests.expect"
local t = elm_test

local tests = {}

-- ── Values ───────────────────────────────────────────────────────────────────

function tests.integers()
    expect.eq(t.twice(21), 42)
    expect.eq(t.twice(3.0), 6, "a float with an integer value is accepted")
    expect.error(function() t.twice(1.5) end, "number has no integer representation")
    expect.error(function() t.twice("x") end, "number expected")
    expect.eq(t.byte(255), 255)
    expect.error(function() t.byte(256) end, "integer out of range")
    expect.error(function() t.byte(-1) end, "integer out of range")
end

function tests.enums()
    expect.eq(t.next_color(1), 2)
    expect.eq(t.next_color(3), 1)
end

function tests.strings()
    expect.eq(t.echo("hello"), "hello")
    expect.eq(t.echo("with\0zero"), "with\0zero", "embedded zeros survive")
    expect.eq(t.concat("a", "b"), "ab")
    expect.error(function() t.echo({}) end, "string expected")
end

function tests.module_values()
    expect.eq(t.answer, 42)
    expect.eq(t.name, "elm")
    expect.eq(package.loaded["elm_test"], t, "a module is also a loaded package")
    expect.eq(require "elm_test", t)
end

function tests.multiple_results()
    local a, b, c = t.triple(1)
    expect.eq(a, 1)
    expect.eq(b, "two")
    expect.eq(c, true)
end

function tests.optional()
    expect.eq(t.maybe(true), 5)
    expect.eq(t.maybe(false), nil)
    expect.eq(t.or_default(3), 3)
    expect.eq(t.or_default(nil), -1)
    expect.eq(t.or_default(), -1, "a missing argument is nil")
end

-- ── Errors ───────────────────────────────────────────────────────────────────

function tests.cpp_exception_becomes_lua_error()
    expect.error(t.throw, "thrown from C++")
end

function tests.lua_error_unwinds_cpp_frames()
    local before = t.tracked_alive()
    expect.error(t.raise_with_live_object, "raised over a live C++ object")
    expect.eq(t.tracked_alive(), before, "the C++ object on the unwound frame was destroyed")
end

function tests.engine_result()
    expect.eq(t.checked(false), 10)
    expect.error(function() t.checked(true) end, "checked failure")
    expect.eq(select("#", t.checked_void(false)), 0, "EngineResult<void> returns nothing")
    expect.error(function() t.checked_void(true) end, "void failure")
end

function tests.errors_name_the_argument()
    expect.error(function() t.concat("a", {}) end, "bad argument #2")
    expect.error(function() t.TestVector(1, 2, 3):dot(5) end, "bad argument #1 to 'dot'")
end

-- ── Functions ────────────────────────────────────────────────────────────────

function tests.lambdas()
    expect.eq(t.subtract(10, 3), 7)
end

function tests.raw_c_function()
    expect.eq(t.sum(), 0)
    expect.eq(t.sum(1, 2, 3.5), 6.5)
end

function tests.state_injection()
    expect.eq(t.stack_size(99), 1, "lua_State* takes no argument slot")
end

function tests.callbacks()
    expect.eq(t.call_back(function(v) return v * 10 end, 4), 40)
    expect.eq(t.call_back(function() error("inside callback") end, 4), -1, "a failing callback reports to C++")
    expect.error(function() t.call_back(42, 1) end, "function expected")
end

function tests.references()
    local value = { marker = true }
    t.store(value)
    expect.eq(t.restore(), value, "a Ref keeps the same table")
    t.store(nil)
    expect.eq(t.restore(), nil)
end

-- ── Classes ──────────────────────────────────────────────────────────────────

function tests.class_construction()
    local v = t.TestVector(1, 2, 3)
    expect.eq(v.x, 1)
    expect.eq(v.y, 2)
    expect.eq(v.z, 3)
    local w = t.TestVector.new(4, 5, 6)
    expect.eq(w.z, 6)
    expect.error(function() t.TestVector(1, 2) end, "number expected")
end

function tests.class_fields()
    local v = t.TestVector(1, 2, 3)
    v.x = 10
    expect.eq(v.x, 10)
    expect.error(function() v.w = 1 end, "TestVector has no writable field 'w'")
    expect.eq(v.w, nil, "unknown fields read as nil")
    expect.error(function() v.x = "text" end, "number expected")
end

function tests.class_properties()
    local v = t.TestVector(3, 4, 0)
    expect.near(v.length, 5)
    expect.error(function() v.length = 1 end, "no writable field 'length'")

    local tracked = t.Tracked(5)
    expect.eq(tracked.value, 5)
    tracked.value = 8
    expect.eq(tracked.value, 8)
    expect.error(function() tracked.value = -1 end, "value must not be negative")
    expect.eq(tracked.id, 7)
    expect.error(function() tracked.id = 1 end, "no writable field 'id'")
end

function tests.class_methods()
    local v = t.TestVector(1, 2, 3)
    expect.eq(v:dot(t.TestVector(1, 1, 1)), 6)
    v:scale(2)
    expect.eq(v.z, 6, "a method changes the Lua object")
    local n = t.TestVector(0, 3, 4)
    n:normalize()
    expect.near(n.length, 1)
    expect.eq(t.TestVector.up().y, 1)
    expect.error(function() v.dot(42, v) end, "TestVector expected")
    expect.error(function() v:dot(t.Tracked(1)) end, "TestVector expected")
    expect.eq(t.Tracked(21):doubled(), 42)
end

function tests.class_metamethods()
    local sum = t.TestVector(1, 2, 3) + t.TestVector(1, 1, 1)
    expect.eq(sum.x, 2)
    expect.eq(sum.z, 4)
    expect.eq(tostring(sum), "TestVector(2, 3, 4)")
    expect.eq(t.TestVector(1, 2, 3), t.TestVector(1, 2, 3), "__eq for comparable types")
    expect.eq(t.TestVector(1, 2, 3) == t.TestVector(3, 2, 1), false)
end

function tests.values_are_copies()
    local a = t.TestVector(1, 2, 3)
    local b = a + t.TestVector(0, 0, 0)
    b.x = 100
    expect.eq(a.x, 1)
end

function tests.gc_destroys_objects()
    collectgarbage("collect")
    local before = t.tracked_alive()
    local keep = {}
    for i = 1, 100 do
        keep[i] = t.Tracked(i)
    end
    expect.eq(t.tracked_alive(), before + 100)
    keep = nil
    collectgarbage("collect")
    expect.eq(t.tracked_alive(), before)
end

return tests
