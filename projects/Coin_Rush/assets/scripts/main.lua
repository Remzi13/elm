-- The main module of the game (entry.mainModule): it runs in Play only, from a fresh Lua state.
local main = {}

local Vector3 = elm.math.Vector3

function main.on_start()
    elm.camera.set_position(Vector3(0, 35, -35))
    elm.camera.look_at(Vector3(0, 0, 0))
end

function main.on_update(dt)
    local game = elm.scene.find("game")
    if game and game.script and game.script:wants_next_scene() then
       elm.engine.load_scene(game.script.next_scene)
    end
end

return main
