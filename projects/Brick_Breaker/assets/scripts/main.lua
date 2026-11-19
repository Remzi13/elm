-- The main module of the game (entry.mainModule): it runs in Play only, from a fresh Lua state.
local main = {}

function main.on_start()   
end

function main.on_update(dt)
    local game = elm.scene.find("game")
    if game and game.script and game.script:wants_next_scene() then
       elm.engine.load_scene(game.script.next_scene)
    end
end

return main
