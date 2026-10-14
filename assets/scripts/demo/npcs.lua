-- Demo: NPCs with their own logic. Every guard is an entity with the npc.guard script: it patrols
-- around its post and chases the player cube when it sees it. Arrow keys move the player.
-- Guards and the player are kinematic bodies: they push the crates around, and a crate between a
-- guard and the player hides the player (a raycast in npc/guard.lua).
-- Set Script/MainModule to "demo.npcs" to run it. Edit npc/guard.lua and save: all guards run the
-- new code at once and keep their state.
local Vector3, Vector4 = elm.math.Vector3, elm.math.Vector4
local input = elm.input

local demo = {}

local GUARDS = 8
local POST_RADIUS = 14
local CRATES = 24
local PLAYER_SPEED = 6

local player

function demo.build_scene()
    elm.engine.clear_scene()

    local ground = elm.scene.spawn("Ground", elm.models.plane(60, 60))
    ground.color = Vector4(0.30, 0.34, 0.30, 1)
    ground:add("Collider") -- fits the plane: a thin box under it

    local cube = elm.models.cube(1)
    player = elm.scene.spawn("Player", cube)
    player.position = Vector3(0, 0.5, 0)
    player.color = Vector4(0.20, 0.60, 1.00, 1)
    player:add("Collider")
    player:add("RigidBody", { type = "kinematic" })

    for i = 1, GUARDS do
        local angle = (i - 1) / GUARDS * 2 * math.pi
        local guard = elm.scene.spawn("Guard " .. i, cube)
        guard.position = Vector3(math.cos(angle) * POST_RADIUS, 0.5, math.sin(angle) * POST_RADIUS)
        guard:add("Collider")
        guard:add("RigidBody", { type = "kinematic" })
        -- Each guard gets its own props on top of the defaults of npc/guard.lua
        guard:add("Script", { module = "npc.guard", props = { speed = 2 + i * 0.25, line_of_sight = true } })
    end

    local crate_model = elm.models.cube(1.2)
    for i = 1, CRATES do
        local angle = i / CRATES * 2 * math.pi
        local radius = 6 + (i % 3) * 2.5
        local crate = elm.scene.spawn("Crate " .. i, crate_model)
        crate.position = Vector3(math.cos(angle) * radius, 0.6, math.sin(angle) * radius)
        crate.color = Vector4(0.55, 0.40, 0.25, 1)
        crate:add("Collider")
        crate:add("RigidBody", { mass = 3, friction = 0.8 })
    end

    elm.camera.set_position(Vector3(0, 22, -26))
    elm.camera.look_at(Vector3(0, 0, 0))
    print(string.format("demo.npcs: %d guards and %d crates around the player, arrow keys move it", GUARDS, CRATES))
end

function demo.on_start()
    demo.build_scene()
end

-- This file changed: build the scene with the new code
function demo.on_reload()
    demo.build_scene()
end

function demo.on_update(dt)
    if not (player and player.valid) then
        return
    end
    -- Relative to the camera, over the ground
    local forward = elm.camera.forward()
    local right = elm.camera.right()
    forward.y, right.y = 0, 0
    local move = Vector3.zero()
    if input.is_down(input.key.Up) then move = move + forward end
    if input.is_down(input.key.Down) then move = move - forward end
    if input.is_down(input.key.Right) then move = move + right end
    if input.is_down(input.key.Left) then move = move - right end
    if move.length > 0 then
        player.position = player.position + move:normalized() * PLAYER_SPEED * dt
    end
end

return demo
