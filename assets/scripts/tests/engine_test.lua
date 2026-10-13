-- elm.time, elm.settings, elm.input, elm.camera.
local expect = require "tests.expect"
local Vector3 = elm.math.Vector3

local tests = {}

function tests.time()
    expect.eq(type(elm.time.delta()), "number")
    expect.eq(type(elm.time.now()), "number")
    expect.eq(math.type(elm.time.frame()), "integer")
end

function tests.settings_read()
    expect.eq(elm.settings.get("Script/MainModule"), "main")
    expect.eq(elm.settings.get("Script/Enabled"), true)
    expect.eq(elm.settings.get("Script/Missing"), nil)
end

function tests.settings_write_keeps_the_type()
    elm.settings.set("Script/MainModule", "game")
    expect.eq(elm.settings.get("Script/MainModule"), "game")
    elm.settings.set("Script/MainModule", "main")
    expect.error(function() elm.settings.set("Script/Enabled", "yes") end, "boolean expected")
    expect.error(function() elm.settings.set("Script/Missing", 1) end, "unknown setting 'Script/Missing'")
end

function tests.input_without_window()
    local input = elm.input
    expect.eq(input.is_down(input.key.W), false)
    expect.eq(input.is_pressed(input.key.Space), false)
    expect.eq(input.is_mouse_down(input.mouse.Left), false)
    local x, y = input.mouse_position()
    expect.eq(type(x), "number")
    expect.eq(type(y), "number")
    expect.eq(input.axis("MoveForward"), 0)
    expect.eq(input.key.A, 65, "key codes are the engine's")
end

function tests.camera()
    local camera = elm.camera
    camera.set_position(Vector3(0, 0, 0))
    expect.eq(camera.position(), Vector3(0, 0, 0))
    camera.translate(Vector3(1, 2, 3))
    expect.eq(camera.position(), Vector3(1, 2, 3))

    camera.set_position(Vector3(0, 0, 0))
    camera.look_at(Vector3(10, 0, 0))
    expect.near(camera.forward().x, 1, 1e-4)
    camera.look_at(Vector3(0, 0, -5))
    expect.near(camera.forward().z, -1, 1e-4)
    camera.set_orientation(0, 10)
    expect.eq(camera.pitch() < 1.56, true, "pitch is clamped")

    camera.set_fov(1.2)
    expect.near(camera.fov(), 1.2)
end

return tests
