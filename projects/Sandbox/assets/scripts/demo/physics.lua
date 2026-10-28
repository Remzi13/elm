-- Demo: physics components. A pyramid of crates, a launch pad (a trigger with the
-- gameplay.launch_pad script) and stones thrown from the camera with Space (gameplay.projectile).
-- Set entry.mainModule of Sandbox.elmproject to "demo.physics" to run it. R rebuilds the scene.
local Vector3, Vector4 = elm.math.Vector3, elm.math.Vector4
local input = elm.input

local demo = {}

local PYRAMID = 6
local THROW_SPEED = 20

local stone_model

function demo.build_scene()
    elm.engine.clear_scene()

    local ground = elm.scene.spawn("Ground", elm.models.plane(60, 60))
    ground.color = Vector4(0.30, 0.34, 0.30, 1)
    ground:add("Collider") -- fits the plane: a thin box under it

    -- Without sizes a Collider fits the entity's model
    local cube = elm.models.cube(1)
    for layer = 0, PYRAMID - 1 do
        local count = PYRAMID - layer
        for i = 0, count - 1 do
            local crate = elm.scene.spawn("Crate", cube)
            crate.position = Vector3((i - (count - 1) / 2) * 1.02, 0.5 + layer, 6)
            crate.color = Vector4(0.55 + layer * 0.07, 0.40, 0.25, 1)
            crate:add("Collider")
            crate:add("RigidBody", { mass = 2, friction = 0.6 })
        end
    end

    -- The trigger volume reaches above the plate: a falling body is seen before it is through
    local pad = elm.scene.spawn("Launch pad", elm.models.box(Vector3(1.5, 0.05, 1.5)))
    pad.position = Vector3(-6, 0.05, 2)
    pad.color = Vector4(0.20, 0.90, 0.40, 1)
    pad:add("Collider", { half_extents = Vector3(1.5, 0.5, 1.5), offset = Vector3(0, 0.5, 0), trigger = true })
    pad:add("Script", { module = "gameplay.launch_pad", props = { launch_speed = 12 } })

    for i = 1, 4 do
        local crate = elm.scene.spawn("Bouncing crate", cube)
        crate.position = Vector3(-6 + (i - 2.5) * 0.6, 3 + i * 1.5, 2)
        crate.color = Vector4(0.30, 0.50, 0.90, 1)
        crate:add("Collider")
        crate:add("RigidBody", { restitution = 0.2 })
    end

    stone_model = elm.models.cube(0.5)
    elm.camera.set_position(Vector3(0, 5, -12))
    elm.camera.look_at(Vector3(0, 1.5, 5))
    print("demo.physics: Space throws a stone, R rebuilds the scene")
end

function demo.throw()
    -- Stop removes the models made during Play (R rebuilds them): make it again when it is gone
    if not (stone_model and stone_model.valid) then
        stone_model = elm.models.cube(0.5)
    end
    local forward = elm.camera.forward()
    local stone = elm.scene.spawn("Stone", stone_model)
    stone.position = elm.camera.position() + forward * 1.5
    stone.color = Vector4(0.90, 0.50, 0.20, 1)
    stone:add("Collider")
    -- The body is made at once: the velocity can be set right away
    local body = stone:add("RigidBody", { mass = 4, restitution = 0.3 })
    body.velocity = forward * THROW_SPEED
    stone:add("Script", { module = "gameplay.projectile", props = { lifetime = 10 } })
end

function demo.on_start()
    demo.build_scene()
end

function demo.on_reload()
    demo.build_scene()
end

function demo.on_update(dt)
    if input.is_pressed(input.key.Space) then
        demo.throw()
    end
    if input.is_pressed(input.key.R) then
        demo.build_scene()
    end
end

return demo
