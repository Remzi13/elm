-- The demo and gameplay modules load and have what the engine calls. Running them needs the
-- renderer (models), so this only catches modules that do not load.
local expect = require "tests.expect"

local tests = {}

function tests.demos_load()
    for _, name in ipairs({ "demo.spinner", "demo.npcs", "demo.physics" }) do
        local demo = require(name)
        expect.eq(type(demo.on_start), "function", name .. ".on_start")
    end
end

function tests.entity_scripts_load()
    for _, name in ipairs({ "npc.guard", "gameplay.launch_pad", "gameplay.projectile" }) do
        local class = require(name)
        expect.eq(type(class), "table", name)
        expect.eq(type(class.props), "table", name .. ".props")
    end
end

return tests
