-- elm.debug: drawing calls and their styles, script channels.
local expect = require "tests.expect"
local Vector3 = elm.math.Vector3
local Vector4 = elm.math.Vector4

local tests = {}

local origin = Vector3(0, 0, 0)
local up = Vector3(0, 1, 0)

function tests.shapes_accept_no_style()
    elm.debug.line(origin, up)
    elm.debug.arrow(origin, up)
    elm.debug.ray(origin, up, 2)
    elm.debug.point(up)
    elm.debug.box(origin, Vector3(0.5, 0.5, 0.5))
    elm.debug.sphere(origin, 1)
    elm.debug.capsule(origin, 0.5, 1)
    elm.debug.text(up, "label")
end

function tests.text_takes_a_style()
    elm.debug.text(up, "timed", { color = Vector4(1, 1, 0, 1), duration = 1, channel = elm.debug.channel("tests") })
    expect.error(function() elm.debug.text(up, "x", { size = 2 }) end, "no field 'size'")
end

function tests.style_fields()
    local channel = elm.debug.channel("tests")
    elm.debug.line(origin, up, { color = Vector4(1, 0, 0, 1), duration = 0.5, on_top = true, channel = channel })
    elm.debug.point(up, 0.5, { on_top = false })
end

function tests.style_errors()
    expect.error(function() elm.debug.line(origin, up, { colour = Vector4(1, 0, 0, 1) }) end, "no field 'colour'")
    expect.error(function() elm.debug.line(origin, up, { color = Vector3(1, 0, 0) }) end, "color must be a Vector4")
    expect.error(function() elm.debug.line(origin, up, { duration = -1 }) end, "duration must be a number")
    expect.error(function() elm.debug.line(origin, up, { channel = "tests" }) end, "channel must come from elm.debug.channel")
    expect.error(function() elm.debug.line(origin, up, 5) end, "style is a table")
end

function tests.channel_is_named_and_switches()
    local channel = elm.debug.channel("switches")
    expect.eq(channel.name, "switches")
    expect.eq(channel.enabled, true)
    channel.enabled = false
    expect.eq(elm.debug.channel("switches").enabled, false)
    channel.enabled = true
    expect.eq(elm.debug.channel("hidden_by_default", false).enabled, false)
end

function tests.channel_name_rules()
    expect.error(function() elm.debug.channel("") end, "non-empty")
    expect.error(function() elm.debug.channel("a/b") end, "no '/'")
end

return tests
