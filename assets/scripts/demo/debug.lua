-- Demo: debug drawing. Set Script/MainModule to "demo.debug" and press Play (F5).
-- A scanner sweeps a fan of rays over a field of pillars and crates; balls fall onto the field.
-- The engine draws its own channels by itself: colliders, triggers, the rays of elm.physics.raycast
-- and the contacts. The script adds three channels of its own (group Script in the Debug menu):
--   sensor  a sphere and the distance on every hit of the scanner, its facing arrow and heading
--   trail   points the balls leave behind, each kept for TRAIL_SECONDS, and their heights
--   shapes  every shape elm.debug has with its name, on the left of the field
-- Toggle any channel in the Debug menu over the viewport. Space drops a ball, R rebuilds the scene.
local Vector3, Vector4 = elm.math.Vector3, elm.math.Vector4
local input = elm.input

local demo = {}

local RAY_COUNT = 9
local FAN_ANGLE = math.rad(60)
local SWEEP_SPEED = 0.6
local SCAN_RANGE = 14
local TRAIL_STEP = 0.1
local TRAIL_SECONDS = 3

local sensor = elm.debug.channel("sensor")
local trail = elm.debug.channel("trail")
local shapes = elm.debug.channel("shapes")

-- Module locals start over on every reload, the persisted table does not
local state = elm.script.persist("demo.debug", { balls = {}, sweep = 0, trail_timer = 0 })

local ball_model

local function spawn_ball(position)
    -- Stop removes the models made during Play: make it again when it is gone
    if not (ball_model and ball_model.valid) then
        ball_model = elm.models.cube(0.5)
    end
    local ball = elm.scene.spawn("Ball", ball_model)
    ball.position = position
    ball.color = Vector4(0.95, 0.55, 0.20, 1)
    ball:add("Collider", { shape = "sphere", radius = 0.25 })
    ball:add("RigidBody", { mass = 1, restitution = 0.5 })
    table.insert(state.balls, ball)
end

function demo.build_scene()
    elm.engine.clear_scene()
    state.balls = {}

    local ground = elm.scene.spawn("Ground", elm.models.plane(40, 40))
    ground.color = Vector4(0.28, 0.31, 0.28, 1)
    ground:add("Collider")

    local pillar_model = elm.models.box(Vector3(0.4, 1.5, 0.4))
    for i = 1, 5 do
        local pillar = elm.scene.spawn("Pillar " .. i, pillar_model)
        pillar.position = Vector3((i - 3) * 3, 1.5, 6 + (i % 2) * 3)
        pillar.color = Vector4(0.45, 0.45, 0.55, 1)
        pillar:add("Collider")
    end

    local crate_model = elm.models.cube(1)
    for i = 1, 4 do
        local crate = elm.scene.spawn("Crate " .. i, crate_model)
        crate.position = Vector3((i - 2.5) * 1.6, 0.5 + i * 1.2, 3)
        crate.color = Vector4(0.60, 0.42, 0.25, 1)
        crate:add("Collider")
        crate:add("RigidBody", { mass = 2 })
    end

    -- A trigger shows in its own channel (Physics/Triggers), and so does its contact
    local zone = elm.scene.spawn("Zone")
    zone.position = Vector3(5, 0.5, 2)
    zone:add("Collider", { half_extents = Vector3(1.5, 0.5, 1.5), trigger = true })

    state.scanner = elm.scene.spawn("Scanner", elm.models.box(Vector3(0.3, 0.3, 0.3)))
    state.scanner.position = Vector3(0, 1.2, -2)
    state.scanner.color = Vector4(0.20, 0.80, 0.90, 1)

    for i = 1, 3 do
        spawn_ball(Vector3(4 + i * 0.3, 4 + i, 2))
    end

    elm.camera.set_position(Vector3(-4, 8, -12))
    elm.camera.look_at(Vector3(0, 0.5, 4))
    print("demo.debug: Play to see the channels, Space drops a ball, R rebuilds the scene")
end

--- A fan of rays from the scanner. The engine draws each ray (Physics/Raycasts); the script
--- marks what it hit, nearer hits redder.
local function scan(dt)
    local scanner = state.scanner
    state.sweep = state.sweep + dt * SWEEP_SPEED
    local heading = math.sin(state.sweep) * math.rad(45)
    local origin = scanner.world_position
    elm.debug.arrow(origin, origin + Vector3(math.sin(heading), 0, math.cos(heading)) * 2, { channel = sensor, on_top = true })
    elm.debug.text(origin + Vector3(0, 0.8, 0), string.format("scanner %+.0f deg", math.deg(heading)), { channel = sensor })

    for i = 0, RAY_COUNT - 1 do
        local angle = heading + (i / (RAY_COUNT - 1) - 0.5) * FAN_ANGLE
        local direction = Vector3(math.sin(angle), -0.05, math.cos(angle))
        local _, point, _, distance = elm.physics.raycast(origin, direction, SCAN_RANGE, scanner)
        if point then
            local near = 1 - distance / SCAN_RANGE
            local style = { color = Vector4(1, 1 - near, 0.2, 1), channel = sensor }
            elm.debug.sphere(point, 0.15, style)
            elm.debug.text(point + Vector3(0, 0.4, 0), string.format("%.1f m", distance), style)
        end
    end
end

--- Each ball leaves a point behind every TRAIL_STEP seconds; the points fade out by themselves.
--- Its height is written over it every frame.
local function leave_trail(dt)
    for _, ball in ipairs(state.balls) do
        if ball.valid then
            local position = ball.world_position
            elm.debug.text(position + Vector3(0, 0.5, 0), string.format("%.2f", position.y), { channel = trail })
        end
    end
    state.trail_timer = state.trail_timer + dt
    if state.trail_timer < TRAIL_STEP then
        return
    end
    state.trail_timer = 0
    for _, ball in ipairs(state.balls) do
        if ball.valid then
            elm.debug.point(ball.world_position, 0.1, { color = Vector4(1, 0.8, 0.3, 1), duration = TRAIL_SECONDS, channel = trail })
        end
    end
end

--- Every shape of elm.debug once, frame by frame. The on_top ones show through the ground.
local function show_shapes()
    local base = Vector3(-10, 0, 4)
    local style = { channel = shapes }
    elm.debug.line(base, base + Vector3(0, 2, 0), style)
    elm.debug.arrow(base + Vector3(1, 0, 0), base + Vector3(1, 2, 0), style)
    elm.debug.ray(base + Vector3(2, 0, 0), Vector3(0, 1, 1), 2, style)
    elm.debug.point(base + Vector3(0, 0.5, 2), 0.4, style)
    elm.debug.box(base + Vector3(0, 0.5, 4), Vector3(0.5, 0.5, 0.5), { color = Vector4(0.4, 0.7, 1, 1), channel = shapes })
    elm.debug.sphere(base + Vector3(2, 0.5, 4), 0.5, { color = Vector4(1, 0.4, 0.7, 1), channel = shapes })
    elm.debug.capsule(base + Vector3(4, 1, 4), 0.4, 0.5, { color = Vector4(0.6, 1, 0.4, 1), channel = shapes })
    elm.debug.sphere(base + Vector3(2, -0.3, 2), 0.6, { color = Vector4(1, 1, 0.3, 1), on_top = true, channel = shapes })

    local names = {
        { "line", Vector3(0, 2.4, 0) }, { "arrow", Vector3(1, 2.4, 0) }, { "ray", Vector3(2, 2.4, 1.4) },
        { "point", Vector3(0, 1, 2) }, { "box", Vector3(0, 1.4, 4) }, { "sphere", Vector3(2, 1.4, 4) },
        { "capsule", Vector3(4, 2.3, 4) }, { "on_top", Vector3(2, 0.6, 2) },
    }
    for _, entry in ipairs(names) do
        elm.debug.text(base + entry[2], entry[1], style)
    end
end

function demo.on_start()
    demo.build_scene()
end

function demo.on_reload()
    demo.build_scene()
end

function demo.on_update(dt)
    if not (state.scanner and state.scanner.valid) then
        demo.build_scene() -- the scene was replaced
    end
    if input.is_pressed(input.key.Space) then
        spawn_ball(Vector3(math.random() * 6 - 3, 6, math.random() * 4 + 1))
    end
    if input.is_pressed(input.key.R) then
        demo.build_scene()
    end
    scan(dt)
    leave_trail(dt)
    show_shapes()
end

return demo
