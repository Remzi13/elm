-- The main module of the game (entry.mainModule): it runs in Play only, from a fresh Lua state.
local main = {}

local Vector3 = elm.math.Vector3

function main.on_start()
    elm.camera.set_position(Vector3(0, 35, -35))
    elm.camera.look_at(Vector3(0, 0, 0))
end

function main.on_update(dt)
end

return main
