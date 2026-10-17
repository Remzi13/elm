-- Tutorial game "Coin Rush" (docs/tutorial/Первая игра.md). Set Script/MainModule to "coin_rush.main".
-- The player cube collects coins and runs from hazards. The logic lives in entity scripts:
--   coin_rush.player  arrows / WASD move the player
--   coin_rush.coin    a spinning trigger: the player takes it
--   coin_rush.hazard  goes back and forth, sends the player back to the start
--   coin_rush.game    counts the coins and fills the score bar
-- This module only builds the scene. Press F5 (Play) to start the game, F5 again to reset it.
local Vector3, Vector4 = elm.math.Vector3, elm.math.Vector4

local main = {}

-- nil: build_scene() makes the scene. A file name ("coin_rush.scene" in assets/scenes): the scene is
-- loaded from it, the way it was saved from the editor.
local SCENE_FILE = nil

local ARENA = 10 -- half size of the ground
local COIN_RING = 7
local COINS = 8

local GROUND_COLOR = Vector4(0.25, 0.30, 0.25, 1)
local PLAYER_COLOR = Vector4(0.20, 0.60, 1.00, 1)
local COIN_COLOR = Vector4(1.00, 0.80, 0.10, 1)
local HAZARD_COLOR = Vector4(0.90, 0.20, 0.20, 1)
local BAR_COLOR = Vector4(0.30, 0.90, 0.40, 1)

local function spawn_ground()
    local ground = elm.scene.spawn("Ground", elm.models.plane(ARENA * 2, ARENA * 2))
    ground.color = GROUND_COLOR
end

local function spawn_player()
    local player = elm.scene.spawn("Player", elm.models.cube(1))
    player.position = Vector3(0, 0.5, 0)
    player.color = PLAYER_COLOR
    -- A trigger only sees moving bodies: the player is a kinematic body that the script moves
    player:add("Collider")
    player:add("RigidBody", { type = "kinematic" })
    player:add("Script", { module = "coin_rush.player", props = { arena = ARENA - 0.5 } })
end

local function spawn_coins()
    local model = elm.models.box(Vector3(0.35, 0.35, 0.08))
    for i = 1, COINS do
        local angle = (i - 1) / COINS * 2 * math.pi
        local coin = elm.scene.spawn("Coin", model)
        coin.position = Vector3(math.cos(angle) * COIN_RING, 0.6, math.sin(angle) * COIN_RING)
        coin.color = COIN_COLOR
        -- A Collider without a RigidBody is a static body; trigger: it reports, it does not push
        coin:add("Collider", { shape = "sphere", radius = 0.6, trigger = true })
        coin:add("Script", "coin_rush.coin")
    end
end

local function spawn_hazards()
    local model = elm.models.cube(1.2)
    local routes = {
        { from = Vector3(-4, 0.6, -4), to = Vector3(4, 0, 0), speed = 1.5 },
        { from = Vector3(4, 0.6, 4), to = Vector3(-8, 0, 0), speed = 1.0 },
        { from = Vector3(-8, 0.6, 0), to = Vector3(0, 0, 8), speed = 2.0 },
    }
    for i, route in ipairs(routes) do
        local hazard = elm.scene.spawn("Hazard " .. i, model)
        hazard.position = route.from
        hazard.color = HAZARD_COLOR
        hazard:add("Script", { module = "coin_rush.hazard", props = { offset = route.to, speed = route.speed } })
    end
end

local function spawn_game()
    -- The score bar is a box along X at the far edge of the arena: game.lua stretches it
    local bar = elm.scene.spawn("Score bar", elm.models.cube(1))
    bar.position = Vector3(0, 0.25, ARENA + 1)
    bar.scale = Vector3(0.01, 0.5, 0.5)
    bar.color = BAR_COLOR

    -- An entity without a model: it only carries the game script
    local game = elm.scene.spawn("Game")
    game:add("Script", { module = "coin_rush.game", props = { bar_length = ARENA * 2 } })
end

function main.build_scene()
    elm.engine.clear_scene()
    spawn_ground()
    spawn_game()
    spawn_player()
    spawn_coins()
    spawn_hazards()
    print(string.format("coin_rush: scene built, %d entities. F5 starts the game", elm.scene.count()))
end

local function set_up_camera()
    elm.camera.set_position(Vector3(0, 26, -20))
    elm.camera.look_at(Vector3(0, 0, -4))
end

local function show_scene()
    if SCENE_FILE then
        elm.engine.load_scene(SCENE_FILE)
    else
        main.build_scene()
    end
    set_up_camera()
end

function main.on_start()
    show_scene()
end

-- This file was saved: show what the new code builds
function main.on_reload()
    show_scene()
end

return main
