-- require through the engine file system and the restricted standard library.
local expect = require "tests.expect"

local tests = {}

function tests.require_runs_a_module_once()
    local first = require "tests.fixtures.counter"
    local second = require "tests.fixtures.counter"
    expect.eq(first, second)
    expect.eq(first.loads, 1)
end

function tests.require_loads_init_lua()
    expect.eq(require("tests.fixtures.pkg").name, "pkg")
end

function tests.missing_module()
    expect.error(function() require "tests.fixtures.missing" end, "no module 'tests.fixtures.missing' in the scripts folder")
end

function tests.module_names_stay_in_the_script_root()
    expect.error(function() require "../outside" end, "invalid module name")
    expect.error(function() require "tests..expect" end, "invalid module name")
    expect.error(function() require "C:\\file" end, "invalid module name")
end

function tests.syntax_errors_name_the_file()
    local message = expect.error(function() require "tests.fixtures.broken" end, "error loading module 'tests.fixtures.broken'")
    expect.eq(message:find("tests/fixtures/broken.lua:2", 1, true) ~= nil, true, message)
end

function tests.no_file_or_native_access()
    expect.eq(io, nil)
    expect.eq(os, nil)
    expect.eq(dofile, nil)
    expect.eq(loadfile, nil)
    expect.eq(package.loadlib, nil)
    expect.eq(#package.searchers, 2, "preload and the engine searcher")
end

function tests.no_bytecode()
    local bytecode = string.dump(function() return 1 end)
    for _, mode in ipairs({ "b", "bt", "t" }) do
        local fn, message = load(bytecode, "dump", mode)
        expect.eq(fn, nil, "mode " .. mode)
        expect.eq(message:find("attempt to load a binary chunk", 1, true) ~= nil, true, message)
    end
    expect.eq(load(bytecode), nil, "the default mode")
    expect.eq(load("return 1")(), 1, "text still loads")
    expect.eq(load("return x", "env", "t", { x = 5 })(), 5, "with an environment")
end

function tests.print_goes_to_the_log()
    print("self test message", 1, true, nil) -- must not fail on any value type
    elm.log.info("info from a test")
    elm.log.warn("warning from a test")
end

return tests
